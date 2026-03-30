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
from fastapi import FastAPI, WebSocket
import pandas as pd
from fastapi import UploadFile, File
from fastapi import HTTPException
import threading

acks = {}
ack_events = {}
ack_lock = threading.Lock()


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
esp32_details =db["esp32_details"]


# -------- MQTT CALLBACKS --------

def on_connect(client, userdata, flags, rc):
    print("MQTT connected:", rc)
    client.subscribe(REQ_TOPIC)
    client.subscribe(STATUS_TOPIC)
    client.subscribe("esp32/ack/+")   # ✅ ACK topic
    client.subscribe("esp32/ack_bulk/+")
    client.subscribe("esp32/ack_bulk_RM_ALL/+")

def on_message(client, userdata, msg):
    payload = msg.payload.decode()
    topic = msg.topic

    print(f"MQTT RX [{topic}]: {payload}")

    # ✅ MUST be FIRST
    if topic.startswith("esp32/ack_bulk/"):
        device_id = topic.split("/")[-1].strip()

        print("🔥 BULK ACK DETECTED")

        try:
            data = json.loads(payload)

            if data.get("status") == "completed":
                print("🔥 BULK COMPLETED MATCH")

                if device_id in ack_events:
                    ack_events[device_id].set()
                    print("✅ EVENT SET SUCCESS")
                else:
                    print("❌ DEVICE NOT FOUND IN EVENTS")

        except Exception as e:
            print("Bulk parse error:", e)

    # ✅ THEN normal ACK
    elif topic.startswith("esp32/ack/"):
        device_id = topic.split("/")[-1]
        print(f"ACK from {device_id}: {payload}")

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
        "version": data.get("Version", "unknown"),
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
    # if topic.startswith("esp32/ack_bulk/"):
    #     device_id = topic.split("/")[-1].strip()

    #     try:
    #         data = json.loads(payload)

    #         if data.get("status") == "completed":

    #             with ack_lock:
    #                 event = ack_events.get(device_id)

    #             if event:
    #                 print("🔥 SETTING EVENT OBJECT:", id(event))
    #                 event.set()
    #             else:
    #                 print("❌ EVENT NOT FOUND")

    #     except Exception as e:
    #         print("ACK parse error:", e)

    if topic.startswith("esp32/ack_bulk_RM_ALL"):
        device_id = topic.split("/")[-1].strip()
    
        if data.get("status") == "success":
            result = esp32_user.delete_many({
                "device_id": device_id
            })
    
            print(f"[BULK_RM] Device: {device_id}, Deleted: {result.deleted_count}")

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





class CommandRequest(BaseModel):
    cmd: str


@app.post("/bulk/{device_id}")
def send_command(device_id: str, request: CommandRequest):
    topic = f"esp32/cmd/{device_id}"

    mqtt_client.publish(
        topic,
        request.cmd,
        qos=1
    )

    return {
        "device": device_id,
        "command": request.cmd,
        "status": "sent"
    }


@app.get("/send/{device_id}")
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
        "ack": acks.pop(device_id, "no_ack")
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
##curl -X GET "http://127.0.0.1:8000/send/esp32_001?cmd={RESET:RESET}"                                     #CMD for Reset
















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




class ESP32DetailsModel(BaseModel):
    device_id: str
    site: str
    floor: str
    cabin: str


@app.post("/add-esp32")
def esp32_details(data: ESP32DetailsModel):
    try:
        document = {
            "device_id": data.device_id,
            "site": data.site,
            "floor": data.floor,
            "cabin": data.cabin,
            "created_at": datetime.utcnow()
        }

        result = db["esp32_details"].insert_one(document)

        return {
            "message": "Data inserted successfully",
            "inserted_id": str(result.inserted_id)
        }

    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))



@app.get("/delete-esp32/{device_id}")
def delete_esp32(device_id: str):
    try:
        result = db["esp32_details"].delete_one(
            {"device_id": device_id}
        )

        if result.deleted_count == 0:
            raise HTTPException(
                status_code=404,
                detail="Device not found"
            )

        return {
            "message": "Device deleted successfully",
            "device_id": device_id
        }

    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.put("/update-esp32/{device_id}")
def update_esp32(device_id: str, data: ESP32DetailsModel):
    try:
        update_data = {
            "site": data.site,
            "floor": data.floor,
            "cabin": data.cabin,
            "updated_at": datetime.utcnow()
        }

        result = db["esp32_details"].update_one(
            {"device_id": device_id},
            {"$set": update_data}
        )

        if result.matched_count == 0:
            raise HTTPException(
                status_code=404,
                detail="Device not found"
            )

        return {
            "message": "Device updated successfully",
            "device_id": device_id
        }

    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))




@app.get("/get-user/{device_id}")
def get_users_by_device(device_id: str):
    try:
        documents = list(
            db["esp32_user"].find({"device_id": device_id})
        )

        if not documents:
            raise HTTPException(
                status_code=404,
                detail="No records found for this device"
            )

        for doc in documents:
            doc["_id"] = str(doc["_id"])

        return {
            "count": len(documents),
            "device_id": device_id,
            "data": documents
        }

    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))








@app.get("/get-esp32")
def get_all_esp32():
    try:
        documents = []

        for doc in db["esp32_details"].find():
            doc["_id"] = str(doc["_id"])   # convert ObjectId to string
            documents.append(doc)

        return {
            "count": len(documents),
            "data": documents
        }

    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))




@app.get("/health-esp32")
def get_latest_health_per_device():
    try:
        pipeline = [
            {"$sort": {"_id": -1}},  # newest first
            {
                "$group": {
                    "_id": "$device_id",
                    "latest_log": {"$first": "$$ROOT"}
                }
            }
        ]

        results = list(db["esp32_health"].aggregate(pipeline))

        response = []

        for item in results:
            log = item["latest_log"]
            log["_id"] = str(log["_id"])
            response.append(log)

        return {
            "count": len(response),
            "data": response
        }

    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))




#end point to read the RFID number

latest_data = None

@app.get("/get_string")
def get_string(text: str):
    global latest_data
    latest_data = text
    return {"received": text}


@app.get("/display")
def display():
    global latest_data

    if latest_data is None:
        return {"data": "No card found"}

    temp = latest_data
    latest_data = None   # clear after reading

    return {"data": temp}










# //--------------------bulk update -------------------------------------




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



# sudo systemctl restart mainapp.service    // restarts the systemd services
# sudo journalctl -u mainapp -f              //display the logs
#sudo systemctl restart nginx               //restarts the nginx








@app.post("/upload_excel/{device_id}")
async def upload_excel(device_id: str, file: UploadFile = File(...)):

    print("🔥 API HIT")

    # ✅ Read Excel
    df = pd.read_excel(file.file)

    # ✅ Normalize column names (VERY IMPORTANT)
    df.columns = df.columns.str.strip().str.lower()

    print("📊 Columns detected:", df.columns.tolist())

    # ✅ Check columns flexibly
    if "rfid" not in df.columns or "gmail" not in df.columns:
        return {
            "error": f"Columns not found. Found columns: {df.columns.tolist()}"
        }

    # ✅ Create records (RFID + Gmail)
    records = df[["rfid", "gmail"]].dropna().to_dict(orient="records")

    # Clean values
    for r in records:
        r["rfid"] = str(r["rfid"]).strip()
        r["gmail"] = str(r["gmail"]).strip()

    CHUNK_SIZE = 80
    chunks = [records[i:i + CHUNK_SIZE] for i in range(0, len(records), CHUNK_SIZE)]

    topic = f"esp32/cmd/{device_id}"

    print(f"📦 Total Records: {len(records)}")
    print(f"📦 Total Chunks: {len(chunks)}")

    # ✅ Event setup
    if device_id not in ack_events:
        ack_events[device_id] = threading.Event()

    event = ack_events[device_id]

    for i, chunk in enumerate(chunks):

        # ✅ Extract RFID list
        rfid_list = [item["rfid"] for item in chunk]

        payload = "bulk_add={" + ",".join(rfid_list) + "}"

        print(f"\n🚀 Sending chunk {i+1}/{len(chunks)}")

        event.clear()

        mqtt_client.publish(topic, payload, qos=1)

        time.sleep(0.05)

        print("⏳ Waiting for ACK...")

        # ✅ Wait for ACK
        if not event.wait(timeout=15):
            print(f"❌ Timeout at chunk {i+1}")
            return {"error": f"Timeout at chunk {i+1}"}

        print(f"✅ ACK received for chunk {i+1}")

        # ✅ Save chunk to MongoDB
        print(f"💾 Saving chunk {i+1}...")

        for item in chunk:
            document = {
                "device_id": device_id,
                "RFID": item["rfid"],
                "gmail": item["gmail"]
            }

            esp32_user.update_one(
                {
                    "device_id": document["device_id"],
                    "RFID": document["RFID"],
                    "gmail": document["gmail"]
                },
                {"$setOnInsert": document},
                upsert=True
            )

        print(f"✅ Chunk {i+1} saved successfully")

    return {
        "device": device_id,
        "total_records": len(records),
        "chunks": len(chunks),
        "status": "completed"
    }