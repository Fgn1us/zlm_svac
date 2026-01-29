#include "Common/config.h"
#include "Player/PlayerProxy.h"
#include "Poller/EventPoller.h"
#include "Pusher/MediaPusher.h"
#include "Util/NoticeCenter.h"
#include "Util/logger.h"
#include <iostream>
#include <map>
#include <mutex>
#include <signal.h>

using namespace std;
using namespace toolkit;
using namespace mediakit;

class StreamRelay {
public:
    using Ptr = std::shared_ptr<StreamRelay>;

    StreamRelay(const string &src_url, const string &dst_url, const string &custom_app = "", const string &custom_stream = "")
        : src_url_(src_url)
        , dst_url_(dst_url) {

        // 解析源URL
        MediaInfo src_info(src_url);

        // 使用自定义的app/stream或从URL解析
        if (!custom_app.empty() && !custom_stream.empty()) {
            tuple_ = MediaTuple(src_info.vhost.empty() ? DEFAULT_VHOST : src_info.vhost, custom_app, custom_stream, "");
        } else {
            // 从URL自动解析app和stream
            tuple_ = MediaTuple(
                src_info.vhost.empty() ? DEFAULT_VHOST : src_info.vhost, src_info.app.empty() ? "app" : src_info.app,
                src_info.stream.empty() ? "stream" : src_info.stream, "");
        }

        src_schema_ = src_info.schema;

        // 解析目标URL的schema
        MediaInfo dst_info(dst_url);
        dst_schema_ = dst_info.schema;

        InfoL << "创建流代理: " << src_url << " -> " << dst_url;
        InfoL << "使用标识: " << tuple_.vhost << "/" << tuple_.app << "/" << tuple_.stream;
    }

    bool start() {
        try {
            auto poller = EventPollerPool::Instance().getPoller();

            ProtocolOption option;
            option.enable_hls = false;
            option.enable_mp4 = false;

            // 创建播放器
            player_ = std::make_shared<PlayerProxy>(tuple_, option, -1, poller);

            // 设置播放结果回调
            player_->setOnPlayResult([this](const SockException &ex) {
                if (ex) {
                    ErrorL << "拉流失败 [" << src_url_ << "]: " << ex.what();
                } else {
                    InfoL << "拉流成功 [" << src_url_ << "]";
                }
            });

            // 监听MediaSource注册事件
            auto tag = std::make_shared<int>(0);
            NoticeCenter::Instance().addListener(tag, Broadcast::kBroadcastMediaChanged, [this, poller, tag](BroadcastMediaChangedArgs) {
                // 检查是否是我们关心的流注册了
                if (bRegist && sender.getMediaTuple() == tuple_) {
                    InfoL << "媒体源已就绪，开始推流";

                    // 创建推流器
                    createPusher(poller, dst_schema_, tuple_.vhost, tuple_.app, tuple_.stream, dst_url_);

                    // 移除监听器
                    NoticeCenter::Instance().delListener(tag, Broadcast::kBroadcastMediaChanged);
                }
            });

            // 开始播放
            player_->play(src_url_);
            return true;
        } catch (const std::exception &e) {
            ErrorL << "启动代理失败: " << e.what();
            return false;
        }
    }

    void stop() {
        if (player_) {
            player_->setOnPlayResult(nullptr);
            player_.reset();
        }
        if (pusher_) {
            pusher_.reset();
        }
        if (reconnect_timer_) {
            reconnect_timer_.reset();
        }
        InfoL << "代理已停止";
    }

    string getStatus() const {
        string status = "源URL: " + src_url_ + "\n";
        status += "目标URL: " + dst_url_ + "\n";
        status += "状态: ";
        if (player_ && pusher_) {
            status += "运行中";
        } else if (player_ && !pusher_) {
            status += "拉流中，等待推流";
        } else {
            status += "已停止";
        }
        return status;
    }

private:
    void createPusher(const EventPoller::Ptr &poller, const string &schema, const string &vhost, const string &app, const string &stream, const string &url) {

        pusher_.reset(new MediaPusher(schema, vhost, app, stream, poller));

        pusher_->setOnShutdown([this, poller, schema, vhost, app, stream, url](const SockException &ex) {
            WarnL << "推流中断 [" << url << "]: " << ex.what();
            // 延迟5秒重试
            reconnectPusher(poller, schema, vhost, app, stream, url, 5.0);
        });

        pusher_->setOnPublished([this, url](const SockException &ex) {
            if (ex) {
                ErrorL << "推流失败 [" << url << "]: " << ex.what();
            } else {
                InfoL << "推流成功 [" << url << "]";
            }
        });

        pusher_->publish(url);
    }

    void reconnectPusher(
        const EventPoller::Ptr &poller, const string &schema, const string &vhost, const string &app, const string &stream, const string &url, float delay) {

        reconnect_timer_ = std::make_shared<Timer>(
            delay,
            [this, poller, schema, vhost, app, stream, url]() {
                InfoL << "尝试重新推流";
                createPusher(poller, schema, vhost, app, stream, url);
                return false;
            },
            poller);
    }

private:
    string src_url_;
    string dst_url_;
    string src_schema_;
    string dst_schema_;
    MediaTuple tuple_;
    PlayerProxy::Ptr player_;
    MediaPusher::Ptr pusher_;
    Timer::Ptr reconnect_timer_;
};

// 全局代理管理器
std::map<string, StreamRelay::Ptr> g_relays;
std::mutex g_mutex;

void printUsage() {
    cout << "用法:" << endl;
    cout << "  stream_relay <src_url> <dst_url> [app] [stream]" << endl;
    cout << endl;
    cout << "示例:" << endl;
    cout << "  1. RTSP拉流 -> RTSP推流:" << endl;
    cout << "     stream_relay rtsp://192.168.9.195:19128/live/test rtsp://127.0.0.1:554/live/relay" << endl;
    cout << endl;
    cout << "  2. RTMP拉流 -> RTMP推流:" << endl;
    cout << "     stream_relay rtmp://192.168.9.195:19128/live/test rtmp://127.0.0.1:1935/live/relay" << endl;
    cout << endl;
    cout << "  3. HTTP-FLV拉流 -> RTMP推流:" << endl;
    cout << "     stream_relay http://192.168.9.195:19128/live/test.flv rtmp://127.0.0.1:1935/live/relay" << endl;
    cout << endl;
    cout << "  4. 自定义app和stream:" << endl;
    cout << "     stream_relay rtsp://192.168.9.195:19128/live/test rtsp://127.0.0.1/live/relay myapp mystream" << endl;
}

int main(int argc, char *argv[]) {
    // 设置日志
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    if (argc < 3) {
        printUsage();

        // 如果没有参数，使用默认配置
        cout << endl << "使用默认配置..." << endl;

        // 这里可以根据你的实际情况修改默认配置
        // 假设从RTSP拉流，推送到本地RTSP
        string src_url = "rtsp://192.168.9.195:19128/live/stream";
        string dst_url = "rtsp://127.0.0.1:554/live/relay";

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto relay = std::make_shared<StreamRelay>(src_url, dst_url);
            if (relay->start()) {
                g_relays["default"] = relay;
                cout << "已启动默认代理:" << endl;
                cout << relay->getStatus() << endl;
            }
        }
    } else {
        string src_url = argv[1];
        string dst_url = argv[2];
        string custom_app = (argc > 3) ? argv[3] : "";
        string custom_stream = (argc > 4) ? argv[4] : "";

        std::lock_guard<std::mutex> lock(g_mutex);
        auto relay = std::make_shared<StreamRelay>(src_url, dst_url, custom_app, custom_stream);
        if (relay->start()) {
            string key = src_url; // 使用源URL作为key
            g_relays[key] = relay;
            cout << "代理已启动:" << endl;
            cout << relay->getStatus() << endl;
        }
    }

    // 添加一个定时器，定期打印状态
    auto status_timer = std::make_shared<Timer>(10.0f, []() {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_relays.empty()) {
            cout << "没有活动的代理" << endl;
        } else {
            cout << "====== 代理状态 (" << getTimeStr() << ") ======" << endl;
            for (auto &item : g_relays) {
                cout << item.second->getStatus() << endl;
                cout << "------------------------" << endl;
            }
        }
        return true;
    });

    // 等待退出信号
    static semaphore sem;
    signal(SIGINT, [](int) {
        InfoL << "收到退出信号，清理资源...";

        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto &item : g_relays) {
            item.second->stop();
        }
        g_relays.clear();

        sem.post();
    });

    sem.wait();
    InfoL << "程序退出";
    return 0;
}