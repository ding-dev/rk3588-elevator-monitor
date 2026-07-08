import paho.mqtt.client as mqtt
import json
import time
import threading
import socket
import urllib.request
import os

CMD_SOCK_PATH = "/tmp/elevator_cmd.sock"
TELEMETRY_SOCK_PATH = "/tmp/elevator_telemetry.sock"

# 获取当前脚本所在目录，并创建一个 'ads' 文件夹
AD_DIR = "/home/lsh/Elevator_Smart_System/ads"
os.makedirs(AD_DIR, exist_ok=True) 
print(f"[System] Advertisement directory set to: {AD_DIR}")

# --- 配置 EMQX 参数 ---
BROKER_ADDRESS = "192.168.100.10"  
#BROKER_ADDRESS = "127.0.0.1"  # 因为在 WSL2 本地跑的 docker
BROKER_PORT = 1883
CLIENT_ID = "elevator_device_001"

# --- 定义 MQTT 主题 (Topics) ---
TOPIC_TELEMETRY = "elevator/001/telemetry"  # 上报环境数据
TOPIC_ALERT = "elevator/001/alert"          # 上报故障/AI报警
TOPIC_COMMAND = "elevator/001/command"      # 接收云端指令 (如下发新广告)
TOPIC_STATE = "elevator/001/state"
TOPIC_MEDIA = "elevator/001/media"

def normalize_telemetry_payload(payload):
    try:
        data = json.loads(payload)
    except json.JSONDecodeError:
        return payload

    if "temperature" not in data and "temperature_avg" in data:
        data["temperature"] = data["temperature_avg"]
    if "humidity" not in data and "humidity_avg" in data:
        data["humidity"] = data["humidity_avg"]
    if "status" not in data:
        data["status"] = "normal"

    return json.dumps(data, ensure_ascii=False)

# --- 回调函数：连接成功 (V2 版本写法) ---
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print(f"[MQTT] Successfully connected to EMQX Broker!")
        # 连接成功后，立刻订阅指令主题
        client.subscribe(TOPIC_COMMAND)
        print(f"[MQTT] Subscribed to topic: {TOPIC_COMMAND}")
        client.publish(TOPIC_STATE, json.dumps({"state": "online"}), qos=1, retain=True)
        print("[MQTT] Published online state.")
    else:
        print(f"[MQTT] Connection failed with code {reason_code}")

# ================= [多线程下载任务] =================
def download_and_play(video_url):
    print(f"\n[Thread] 📥 Start downloading ad from: {video_url}")
    # 提取文件名，如果没有就默认叫 current_ad.mp4
    filename = video_url.split("/")[-1] or "current_ad.mp4"
    save_path = os.path.join(AD_DIR, filename)
    
    try:
        urllib.request.urlretrieve(video_url, save_path)
        print(f"[Thread] ✅ Download complete! Saved to {save_path}")
        
        # 下载完后，通过 UDP 通知 C++ 播放
        send_sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        udp_cmd = f"PLAY_AD:{save_path}"
        send_sock.sendto(udp_cmd.encode('utf-8'), CMD_SOCK_PATH)
        send_sock.close()
        print(f">>> [Action] 🎬 Commanded C++ to play {filename}! <<<")
    except Exception as e:
        print(f"[Error] ❌ Failed to download video: {e}")

# --- 回调函数：收到云端消息 ---
def on_message(client, userdata, msg):
    payload = msg.payload.decode('utf-8')
    print(f"\n[MQTT] Received message on {msg.topic}: {payload}")
    
    try:
        command = json.loads(payload)
        
        # 处理警报指令
        if command.get("action") == "sound_alarm":
            level = command.get("level", "high")
            print(f">>> [Action] Triggering ALARM (Level: {level}) to C++! <<<")        
            # 创建一个用于发送的 UDP Socket
            send_sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            send_sock.sendto(b"ALARM_TRIGGER", CMD_SOCK_PATH)
            send_sock.close()

        elif command.get("action") == "cancel_alarm":
            print(f">>> [Action] Canceling ALARM to C++! <<<")
            send_sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            send_sock.sendto(b"ALARM_CANCEL", CMD_SOCK_PATH)
            send_sock.close()   
        
        elif command.get("action") == "switch_ad":
            video_url = command.get("url")
            print(f">>> [MQTT] Received ad update command. Spawning download thread... <<<")
            
            # 🚀 核心：开启新线程去下载，让 MQTT 瞬间回到监听状态！
            # daemon=True 表示主程序退出时，下载线程也会跟着乖乖退出
            threading.Thread(target=download_and_play, args=(video_url,), daemon=True).start()

        elif command.get("action") == "stop_ad":
            print(">>> [Action] 🛑 Stopping Ad! <<<")
            send_sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            send_sock.sendto(b"STOP_AD", CMD_SOCK_PATH)
            send_sock.close()

        elif command.get("action") == "start_camera":
            print(">>> [Action] 🎥 Starting camera stream in C++! <<<")
            send_sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            send_sock.sendto(b"START_CAMERA", CMD_SOCK_PATH)
            send_sock.close()

        elif command.get("action") == "stop_camera":
            print(">>> [Action] 🛑 Stopping camera stream in C++! <<<")
            send_sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            send_sock.sendto(b"STOP_CAMERA", CMD_SOCK_PATH)
            send_sock.close()
    except json.JSONDecodeError:
        print("[MQTT] Failed to decode JSON command")

# --- 核心客户端初始化 (显式声明 V2 API) ---
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, CLIENT_ID)

will_payload = json.dumps({"state": "offline"})
client.will_set(TOPIC_STATE, payload=will_payload, qos=1, retain=True)

client.on_connect = on_connect
client.on_message = on_message

# 连接到 EMQX
client.connect(BROKER_ADDRESS, BROKER_PORT, 60)

# 开启后台线程处理网络通信
client.loop_start()

# ================= 新增：Unix 域套接字本地监听 =================

# 创建 Unix 域 datagram socket
if os.path.exists(TELEMETRY_SOCK_PATH):
    os.unlink(TELEMETRY_SOCK_PATH)

sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
sock.bind(TELEMETRY_SOCK_PATH)

print(f"\n[Local] 🚀 Listening for C++ telemetry on Unix socket: {TELEMETRY_SOCK_PATH}")

try:
    while True:
        # 阻塞等待来自 C++ 的数据 (最大接收 1024 字节)
        data, addr = sock.recvfrom(1024)
        
        # 将 C++ 发来的字节流解码为字符串，并做字段兼容处理
        payload = normalize_telemetry_payload(data.decode('utf-8'))
        parsed = json.loads(payload)

        if parsed.get("type") == "media_status":
            client.publish(TOPIC_MEDIA, payload, qos=1)
            print(f"[MQTT Media] {payload}")
        else:
            client.publish(TOPIC_TELEMETRY, payload, qos=1)
            print(f"[MQTT Upload] {payload}")

except KeyboardInterrupt:
    print("\n[MQTT] Caught Ctrl+C, disconnecting cleanly...")

    # 正常退出前，主动向云端发送离线状态，覆盖掉之前的 retain=True 消息
    client.publish(TOPIC_STATE, json.dumps({"state": "offline"}), qos=1, retain=True)
    # 稍微等 0.5 秒，确保这条离线消息通过网络发出去之后再断开
    import time
    time.sleep(0.5) 
    
    sock.close()
    if os.path.exists(TELEMETRY_SOCK_PATH):
        os.unlink(TELEMETRY_SOCK_PATH)
    client.loop_stop()
    client.disconnect()
