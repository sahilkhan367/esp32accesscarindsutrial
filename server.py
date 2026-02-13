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
from datetime import datetime, timedelta
from fastapi.middleware.cors import CORSMiddleware



BROKER = "localhost"
PORT = 1883

REQ_TOPIC = "esp32/request"
STATUS_TOPIC = "esp32/status/+"
MQTT_TOPIC = "esp32/access/receive"

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


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
            # "reader": reader,
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
##curl -X POST "http://127.0.0.1:8000/send/esp32_001?cmd={gmail:sahil.k@noveloffice.in,ADD:13072052}"      #CMD for Adding card
##curl -X POST "http://127.0.0.1:8000/send/esp32_001?cmd={gmail:sahil.k@noveloffice.in, RM:13072052}"       #CMD for Removing cards
##curl -X POST "http://127.0.0.1:8000/send/esp32_001?cmd={DISPLAY:DATA}"                                    #CMD for Display Data
##curl -X POST "http://127.0.0.1:8000/send/esp32_001?cmd={RESET:RESET}"                                     #CMD for Reset
















####============================== webpage proessing ====================================

from typing import List


class AttendanceRequest(BaseModel):
    email: str
    date: str

def to_dt(date, time):
    return datetime.strptime(f"{date} {time}", "%d-%m-%Y %H:%M:%S")

def sec_to_hms(seconds):
    return str(timedelta(seconds=seconds))



def calculate_attendance(logs, date):
    result = {
        "login_time": None,
        "logout_time": None,
        "break_hours": "00:00:00",
        "effective_login_time": "00:00:00",  # NEW
        "total_login_time": "00:00:00",
        "absent_status": "absent",
        "late_status": None,
        "error": None
    }

    if not logs:
        return result

    logs.sort(key=lambda x: to_dt(date, x["time"]))

    result["absent_status"] = "present"
    result["login_time"] = logs[0]["time"]
    result["logout_time"] = logs[-1]["time"]

    if logs[0]["direction"] == "OUT":
        result["error"] = f"first log IN is missing {logs[0]['time']}"

    if logs[-1]["direction"] == "IN":
        err = f"last log OUT missing {logs[-1]['time']}"
        result["error"] = f"{result['error']}, {err}" if result["error"] else err

    total_login_sec = 0
    break_sec = 0
    last_state = None
    last_time = None

    for log in logs:
        cur_time = to_dt(date, log["time"])
        state = log["direction"]

        if state == last_state:
            err = f"double {state} at {log['time']}"
            result["error"] = f"{result['error']}, {err}" if result["error"] else err

        if state == "IN":
            last_time = cur_time

        elif state == "OUT" and last_time:
            total_login_sec += int((cur_time - last_time).total_seconds())
            last_time = cur_time

        last_state = state

    # Break calculation
    for i in range(len(logs) - 1):
        if logs[i]["direction"] == "OUT" and logs[i + 1]["direction"] == "IN":
            t1 = to_dt(date, logs[i]["time"])
            t2 = to_dt(date, logs[i + 1]["time"])
            break_sec += int((t2 - t1).total_seconds())

    # Assign values properly
    result["break_hours"] = sec_to_hms(break_sec)

    result["effective_login_time"] = sec_to_hms(total_login_sec)

    # Total time = working + break
    overall_sec = total_login_sec + break_sec
    result["total_login_time"] = sec_to_hms(overall_sec)

    result["late_status"] = "late" if result["login_time"] > "10:05:00" else "on time"

    return result




@app.post("/attendance/multi")
def attendance_multi(payload: List[AttendanceRequest]):

    response = []

    for req in payload:
        logs = list(esp32_logs.find(
            {
                "gmail": req.email,
                "date": req.date
            },
            {"_id": 0}
        ))

        result = calculate_attendance(logs, req.date)

        response.append({
            "email": req.email,
            **result
        })

    return response




# http://10.80.4.129:8000/attendance/multi
# [
#   {
#     "email": "sahil.k@noveloffice.in",
#     "date": "10-02-2026"
#   },
#   {
#     "email": "anirudh.k@noveloffice.in",
#     "date": "10-02-2026"
#   }
# ]