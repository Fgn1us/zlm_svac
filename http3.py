import uvicorn
from fastapi import FastAPI, Request
import aiohttp

# --- 配置区 ---
ZLM_API_URL = "http://127.0.0.1:9001"
ZLM_SECRET = "LPvFJlLIeI9spaoSYS3QBJ3AtOcewBYh"
LOCAL_MEDIA_IP = "192.168.9.222"  # 本机媒体流发送/接收 IP
MEDIA_SERVER_ID = "64010000002020000001"

app = FastAPI()


async def get_zlm_data(action: str, params: dict, method: str = "POST"):
    """底层转发逻辑：负责与 ZLM 通信并注入 secret"""
    url = f"{ZLM_API_URL}/index/api/{action}"
    params["secret"] = ZLM_SECRET
    async with aiohttp.ClientSession() as session:
        try:
            if method == "POST":
                async with session.post(url, data=params, timeout=5) as resp:
                    return await resp.json()
            else:
                async with session.get(url, params=params, timeout=5) as resp:
                    return await resp.json()
        except Exception as e:
            print(f"[!] ZLM 请求失败: {e}")
            return None


# --- 1. 处理 INVITE (openRtpServer) ---
@app.post("/index/api/openRtpServer")
async def handle_open_rtp_server(request: Request):
    dynamic_source_ip = request.client.host
    try:
        body = await request.form()
        params_in = dict(body)
    except:
        params_in = {}

    stream_id = params_in.get("stream_id") or params_in.get("streamId")
    zlm_res = await get_zlm_data("openRtpServer", params_in)

    if not zlm_res or zlm_res.get("code") != 0:
        return zlm_res

    rtp_port = zlm_res.get("port")
    sdp_body = (
        f"v=0\r\n"
        f"o={MEDIA_SERVER_ID} 0 0 IN IP4 {LOCAL_MEDIA_IP}\r\n"
        f"s=##ms20091214\r\n"
        f"c=IN IP4 {LOCAL_MEDIA_IP}\r\n"
        f"t=0 0\r\n"
        f"m=video {rtp_port} RTP/AVP 96 98 97\r\n"
        f"a=recvonly\r\n"
        f"a=rtpmap:96 PS/90000\r\n"
        f"a=rtpmap:98 H264/90000\r\n"
        f"a=rtpmap:97 MPEG4/90000\r\n"
    )

    sip_invite_ok = (
        f"SIP/2.0 200 OK\r\n"
        f"Via: SIP/2.0/UDP {dynamic_source_ip}:5060;branch=z9hG4bK-proxy\r\n"
        f"From: <sip:64010000002000000001@{dynamic_source_ip}>;tag=1lad9931d\r\n"
        f"To: <sip:{MEDIA_SERVER_ID}@{LOCAL_MEDIA_IP}>;tag=3094947605\r\n"
        f"Call-ID: wlss-11df50d7-730beb6350a5506aa8316d9dc100cf6b@{dynamic_source_ip}\r\n"
        f"CSeq: 1 INVITE\r\n"
        f"Content-Type: application/sdp\r\n"
        f"Content-Length: {len(sdp_body)}\r\n\r\n{sdp_body}"
    )

    print(f"[*] 点播：回复 INVITE 的 200 OK")
    return {"code": 0, "port": rtp_port, "sip_response": sip_invite_ok}


# --- 2. 处理 BYE (closeRtpServer) ---
@app.post("/index/api/closeRtpServer")
async def handle_close_rtp_server(request: Request):
    dynamic_source_ip = request.client.host
    try:
        body = await request.form()
        params_in = dict(body)
    except:
        params_in = {}

    zlm_res = await get_zlm_data("closeRtpServer", params_in)

    # 构造针对 BYE 的 200 OK (不需要 SDP)
    sip_bye_ok = (
        f"SIP/2.0 200 OK\r\n"
        f"Via: SIP/2.0/UDP {dynamic_source_ip}:5060;branch=z9hG4bK-proxy\r\n"
        f"From: <sip:64010000002000000001@{dynamic_source_ip}>;tag=1lad9931d\r\n"
        f"To: <sip:{MEDIA_SERVER_ID}@{LOCAL_MEDIA_IP}>;tag=3094947605\r\n"
        f"Call-ID: wlss-11df50d7-730beb6350a5506aa8316d9dc100cf6b@{dynamic_source_ip}\r\n"
        f"CSeq: 2 BYE\r\n"
        f"Content-Length: 0\r\n\r\n"
    )

    print(f"[*] 停止：回复 BYE 的 200 OK")
    return {"code": 0, "sip_response": sip_bye_ok}


# --- 3. 其他接口原样转发 ---
@app.api_route("/index/api/{action}", methods=["GET", "POST"])
async def proxy_other_actions(action: str, request: Request):
    method = request.method
    if method == "POST":
        try:
            params = dict(await request.form())
        except:
            params = {}
    else:
        params = dict(request.query_params)
    return await get_zlm_data(action, params, method=method)


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=80)