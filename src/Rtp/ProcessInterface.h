/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */


#ifndef ZLMEDIAKIT_PROCESSINTERFACE_H
#define ZLMEDIAKIT_PROCESSINTERFACE_H

#include <stdint.h>
#include <memory>

namespace mediakit {

class ProcessInterface {
public:
    using Ptr = std::shared_ptr<ProcessInterface>;
    virtual ~ProcessInterface() = default;

    /**
      * 输入rtp
      * @param is_udp 是否为udp模式
      * @param data rtp数据指针
      * @param data_len rtp数据长度
      * @return 是否解析成功
     * Input rtp
     * @param is_udp Whether it is udp mode
     * @param data rtp data pointer
     * @param data_len rtp data length
     * @return Whether the parsing is successful
     
     * [AUTO-TRANSLATED:7d5b06f0]
      */
    virtual bool inputRtp(bool is_udp, const char *data, size_t data_len) = 0;

    /**
     * 刷新输出所有缓存
     * Refresh and output all caches
     
     
     * [AUTO-TRANSLATED:4509b01f]
     */
    virtual void flush() {}

    /**
     * 开始 dump 数据到文件
     * @param dump_dir dump 根目录
     * @param stream_id 流标识，用于文件命名
     * @return 是否成功
     */
    virtual bool startDump(const std::string &dump_dir, const std::string &stream_id, const std::string &prefix = "") { return false; }

    /**
     * 停止 dump 数据
     * @return 是否成功
     */
    virtual bool stopDump() { return false; }

    /**
     * 轮转 dump 文件（关闭当前文件，打开下个整点的新文件）
     * @return 是否成功
     */
    virtual bool rotateFile() { return false; }
};

}//namespace mediakit
#endif //ZLMEDIAKIT_PROCESSINTERFACE_H
