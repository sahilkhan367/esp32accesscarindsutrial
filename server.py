import json
import threading
import time
from fastapi import FastAPI
import paho.mqtt.client as mqtt
from fastapi import Body

BROKER = "localhost"
PORT = 1883

REQ_TOPIC = "esp32/request"
STATUS_TOPIC = "esp32/status/+"
MQTT_TOPIC = "esp32/access/receive"

app = FastAPI()

mqtt_client = mqtt.Client()

# Store device last seen time
devices = {}
acks = {}

# -------- MQTT CALLBACKS --------

def on_connect(client, userdata, flags, rc):
    print("MQTT connected:", rc)
    client.subscribe(REQ_TOPIC)
    client.subscribe(STATUS_TOPIC)
    client.subscribe("esp32/ack/+")   # ✅ ACK topic

def on_message(client, userdata, msg):
    payload = msg.payload.decode()
    topic = msg.topic

    print(f"MQTT RX [{topic}]: {payload}")

    # data = json.loads(payload)
    # device_id = data.get("device_id")

    # if not device_id:
    #     return
    # ACK Message handel    
    if topic.startswith("esp32/ack/"):
        device_id = topic.split("/")[-1]
        acks[device_id] = payload
        print(f"ACK from {device_id}: {payload}")
        return
    # ✅ 2. NOW parse JSON for other messages
    try:
        data = json.loads(payload)
    except json.JSONDecodeError:
        print("Invalid JSON, ignoring")
        return
    device_id = data.get("device_id")
    if not device_id:
        return

    # 🔴🟢 DEVICE STATUS HANDLING (THIS IS THE LINE YOU ASKED ABOUT)
    if topic.startswith("esp32/status"):
        devices[device_id] = data["status"]  # "online" or "offline"
        return

    # 🔁 REQUEST → RESPONSE HANDLING
    if topic == "esp32/request":
        devices[device_id] = "online"  # mark online on any request
        print(f"RFID EVENT from {device_id}: {data}")

        response = {
            "device_id": device_id,
            "status": "ok"
        }

        resp_topic = f"esp32/response/{device_id}"
        mqtt_client.publish(resp_topic, json.dumps(response), qos=1)


# -------- MQTT INIT --------

mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message

def mqtt_loop():
    mqtt_client.connect(BROKER, PORT, 60)
    mqtt_client.loop_forever()

threading.Thread(target=mqtt_loop, daemon=True).start()

# -------- FASTAPI --------


@app.get("/status/{device_id}")
def device_status(device_id: str):
    return {
        "device": device_id,
        "online": devices.get(device_id) == "online"
    }
@app.post("/send/{device_id}")
def send_command(device_id: str, cmd: str):
    topic = f"esp32/cmd/{device_id}"

    mqtt_client.publish(
        topic,
        cmd,
        qos=1,
        #retain=False  #removed becouse of ACK
    )

    return {
        "device": device_id,
        "command": cmd,
        "status": "sent"
    }


@app.get("/ack/{device_id}")
def get_ack(device_id: str):
    return {
        "device": device_id,
        "ack": acks.get(device_id, "no_ack")
    }


@app.get("/")
def root():
    return {"server": "running", "mqtt": "connected"}