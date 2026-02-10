import json
import threading
import time
from fastapi import FastAPI
import paho.mqtt.client as mqtt
from fastapi import Body
from fastapi.responses import FileResponse
import os
from datetime import datetime
from pydantic import BaseModel



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


#==============data base configurations===================

from pymongo import MongoClient

MONGO_URL = "mongodb://localhost:27017"

client = MongoClient(MONGO_URL)

db = client["Transaction_hub"]        # database name
esp32_health = db["esp32_health"]       # collection name
esp32_user = db["esp32_user"]
esp32_logs = db["esp32_logs"]


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

    if topic.startswith("esp32/ack/"):
        device_id = topic.split("/")[-1]
        acks[device_id] = payload
        print(f"ACK from {device_id}: {payload}")
        print("this is payload =====", payload)
        data=json.loads(payload)
        if "ADD" in data:
            print("ADD the crad")
            print("ADD", data["ADD"])
            ADD=data["ADD"].split(":", 1)[1]
            print("gmail:", data["gmail"])
            print("Device_id", data["device_id"])
            document = {
                "device_id": data.get("device_id"),
                "RFID": ADD,
                "gmail": data.get("gmail"),
            }
            esp32_user.update_one(
            {
                "device_id": document["device_id"],
                "gmail": document["gmail"],
                "RFID": document["RFID"]
            },
            {"$setOnInsert": document},
            upsert=True
            )
            
        elif "RM" in data:
            print("REMOVE the crad")
            print("RM", data["RM"])
            RM=data["RM"].split(":", 1)[1]
            print("gmail:", data["gmail"])
            print("Device_id", data["device_id"])
            result = esp32_user.delete_one(
            {
                "device_id": data.get("device_id"),
                "RFID": RM,
                "gmail": data.get("gmail")
            }
        )
        return
    try:
        data = json.loads(payload)
    except json.JSONDecodeError:
        print("Invalid JSON, ignoring")
        return
    device_id = data.get("device_id")
    if not device_id:
        return

    # 🔴🟢 DEVICE STATUS HANDLING
    if topic.startswith("esp32/status"):
        devices[device_id] = data["status"]  # "online" or "offline"
        now_utc = datetime.utcnow()
        now_local = datetime.now()
        esp32_health.insert_one({
        "device_id": device_id,
        "status": data["status"],
        "date": now_local.strftime("%d-%m-%Y"),
        "time": now_local.strftime("%H:%M:%S"),
        })
        return

    # 🔁 REQUEST → RESPONSE HANDLING
    if topic == "esp32/request":
        devices[device_id] = "online"  # mark online on any request
        print(f"RFID EVENT from {device_id}: {data}")
        print("testig log testing==",data["device_id"],"====", data["data"])
        # print(type(data))
        # print(data["data"])
        reader, RFID, direction = data["data"].split(":")
        # print(reader)
        # print(RFID)
        # print(direction)
        device_id=data["device_id"]
        # print(device_id)
        user = esp32_user.find_one(
        {
            "device_id": device_id,
            "RFID": RFID
        },
            {"_id": 0, "gmail": 1}
        )
        gmail = user["gmail"] if user else None
        now = datetime.now()
        log_document = {
            "device_id": device_id,
            "reader": reader,
            "RFID": RFID,
            "direction": direction,
            "gmail": gmail,
            "date": now.strftime("%d-%m-%Y"),
            "time": now.strftime("%H:%M:%S")
        }  
        esp32_logs.insert_one(log_document)
        print("RFID log saved")

        

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



#----------esp32 health and health logs--------


class DeviceStatus(BaseModel):
    device_id: str
    status: str   # online / offline

@app.get("/status/{device_id}")
def device_status(device_id: str):

    return {
        "device": device_id,
        "online": devices.get(device_id) == "online"
    }








@app.post("/send/{device_id}")
def send_command(device_id: str, cmd: str):
    topic = f"esp32/cmd/{device_id}"
    #print("hello hello testing....")

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

FIRMWARE_DIR = "firmware"

@app.get("/firmware/{device_id}")
def download_firmware(device_id: str):
    firmware_file = f"{device_id}.bin"
    firmware_path = os.path.join(FIRMWARE_DIR, firmware_file)

    if not os.path.exists(firmware_path):
        raise HTTPException(status_code=404, detail="Firmware not found")

    return FileResponse(
        firmware_path,
        media_type="application/octet-stream",
        filename=firmware_file
    )

#http://esp32accesshub.novelinfra.com/firmware/esp32_001
# use above url for firmware update


@app.get("/")
def root():
    return {"server": "running", "mqtt": "connected"}



##curl -X POST "http://127.0.0.1:8000/send/esp32_001?cmd=OTA"                                               #cmd for OTA  updates
##curl -X POST "http://127.0.0.1:8000/send/esp32_001?cmd={gmail:sahil.k@noveloffice.in, ADD:13072052}"      #CMD for Adding card
##curl -X POST "http://127.0.0.1:8000/send/esp32_001?cmd={gmail:sahil.k@noveloffice.in, RM:13072052}"       #CMD for Removing cards
##curl -X POST "http://127.0.0.1:8000/send/esp32_001?cmd={DISPLAY:DATA}"                                    #CMD for Display Data
##curl -X POST "http://127.0.0.1:8000/send/esp32_001?cmd={RESET:RESET}"                                     #CMD for Reset