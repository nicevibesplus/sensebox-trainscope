# TrainScope 🚂🤖

**Autonomous Model Railway System powered by senseBox Eye**

## 📖 Project Overview
TrainScope is an autonomous model railway system. It is based on a classic analog model train enhanced with self-driving capabilities. The system operates on 15 V DC electrified tracks and uses a wireless dual-node architecture with two senseBox Eye microcontrollers. 

One unit, located on the train itself, detects trackside signs for speed control and obstacles for emergency braking. It processes sensor data and wirelessly transmits a gear command to a stationary unit. This stationary senseBox adjusts the track voltage accordingly to accelerate, decelerate, or stop the train.

**Motivation:**
The primary motivation behind TrainScope is to demonstrate the integration of modern Edge Artificial Intelligence (Edge AI) and Internet of Things (IoT) technologies into legacy analog infrastructure. By retrofitting a classic model railway with a decentralized sensor network, the project serves as a miniaturized prototyping platform for autonomous driving concepts.

---

## 🏗️ System Architecture

The project is divided into two primary hardware and software nodes, which communicate via Bluetooth.

### 1. The Train (Mobile Unit)
The train is equipped with a dedicated senseBox Eye serving as the central processing unit for sensor data acquisition and decision-making. 
* **Distance Sensor:** A VL53L8CX Time-of-Flight (ToF) sensor continuously measures frontal clearance. If an obstacle is detected within 10 cm, the system triggers an emergency protocol and transmits a "gear 0" stop command.
* **Camera & Wi-Fi:** An onboard HDF3M-811V1 RGB camera captures image data. The train acts as a Wi-Fi Access Point to stream a live feed of this camera to a web interface.
* **Edge AI Sign Detection:** To detect signs in the RGB images, an object detection model was developed using the **Edge Impulse** platform. Specifically, the **FOMO** (Faster Objects, More Objects) architecture was implemented for efficient real-time microcontroller processing.

### 2. Track Controller (Stationary Unit)
The Track Controller functions as the central communication hub and regulates the track voltage. 
* **Power Regulation:** It integrates a senseBox Eye with an L298N dual H-bridge motor driver. The driver receives 15 V DC for the motor load and a 5 V logic supply from the MCU. The MCU uses an 8-bit PWM signal (0-255) to dynamically scale the track voltage between 0 V and 15 V for smooth speed control.
* **Connectivity:** The senseBox operates as a Wi-Fi Access Point, hosting a web dashboard that displays real-time telemetry (like the current gear) and allows manual control. It receives automated gear commands (states 0–2) from the Train via Bluetooth to adjust the PWM output autonomously.

---

## 📂 Repository Structure

This repository contains the source code for both nodes of the TrainScope system:

```text
src/
│
├── train/                             # Source code for the mobile senseBox Eye
│   ├── TrainScope_Object_Detection/   # Exported FOMO model library
│   └── train.ino                      # Sensor reading, ML inference, and Bluetooth TX
│
├── controller/                        # Source code for the stationary senseBox Eye
│   ├── data/                          # HTML/CSS/JS for the dashboard
│   └── controller.ino                 # PWM logic, Motor Driver control, and Bluetooth R
