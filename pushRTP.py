import socket
import time
import struct
import os


def send_rtp_sim(file_path, target_ip, target_port):
    # 1. 创建 UDP Socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    if not os.path.exists(file_path):
        print(f"错误: 找不到文件 {file_path}")
        return

    print(f"开始推送: {file_path} -> {target_ip}:{target_port}")

    try:
        with open(file_path, 'rb') as f:
            packet_count = 0
            while True:
                # 2. 读取 2 字节的长度前缀 (大端模式)
                len_bytes = f.read(2)
                if not len_bytes or len(len_bytes) < 2:
                    print("文件读取完毕，进入下一轮。")
                    f.seek(0, 0) # 移动到最开始
                    time.sleep(0.5)
                    continue

                # 解析长度
                packet_len = struct.unpack('>H', len_bytes)[0]

                # 3. 根据长度读取真正的 RTP 报文内容
                rtp_packet = f.read(packet_len)
                if not rtp_packet:
                    break

                # 4. 通过 UDP 发送
                sock.sendto(rtp_packet, (target_ip, target_port))

                packet_count += 1
                if packet_count % 100 == 0:
                    print(f"已发送 {packet_count} 个 RTP 包...")

                # 5. 控制发送频率 (模拟真实流速)
                # 注意：这里简单的 sleep 是为了防止瞬间冲爆带宽
                # 假设是 25fps 的视频，平均每秒约 30-50 个包，建议间隔 1-5ms
                time.sleep(0.005)

    except Exception as e:
        print(f"推流过程中出现异常: {e}")
    finally:
        sock.close()
        print("Socket 已关闭。")


if __name__ == "__main__":
    # 配置信息
    FILE_PATH = r'D:\Mysoftware\zlm_svac-main\11010600001320000106_1111.rtp'  # 请替换为你的实际文件名
    TARGET_IP = '127.0.0.1'
    TARGET_PORT = 41848 # 默认端口

    send_rtp_sim(FILE_PATH, TARGET_IP, TARGET_PORT)
