# 测试 SVAC 录制回调接收服务
# 用法: python test_svac_hook.py
# 启动后监听 9999 端口，打印收到的 JSON

from http.server import HTTPServer, BaseHTTPRequestHandler
import json

class HookHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length).decode('utf-8')
        print(f"\n========== 收到录制回调 ==========")
        print(f"路径: {self.path}")
        try:
            data = json.loads(body)
            print(f"stream:    {data.get('stream')}")
            print(f"file_name: {data.get('file_name')}")
            print(f"file_path: {data.get('file_path')}")
            print(f"file_size: {data.get('file_size')} bytes")
            start_min = data.get('start_time', 0)
            h, m = divmod(start_min, 60)
            print(f"start_time: {start_min} 分钟 (约 {h:02d}:{m:02d})")
            print(json.dumps(data, indent=2, ensure_ascii=False))
        except:
            print(f"原始body: {body}")
        print(f"===================================\n")
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        self.wfile.write(b'{"code":0}')

    def log_message(self, format, *args):
        pass  # 静默访问日志

if __name__ == '__main__':
    server = HTTPServer(('0.0.0.0', 9999), HookHandler)
    print("SVAC Hook 测试服务已启动: http://127.0.0.1:9999")
    print("等待接收录制回调...\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n已停止")
        server.shutdown()
