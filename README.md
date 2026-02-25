This project presents a real-time wearable health monitoring system built on the ESP32 platform. The system is designed to continuously measure key physiological and environmental parameters and make them accessible through a lightweight embedded web interface. It operates locally without dependency on external cloud services, ensuring availability even in restricted network conditions.

The system monitors heart rate (BPM), blood oxygen saturation (SpO₂), body temperature, ambient temperature, and relative humidity. Sensor data are acquired, processed, and evaluated in real time. An embedded web server hosted directly on the ESP32 provides a browser-based dashboard where all parameters can be viewed instantly over a local Wi-Fi connection.

Beyond simple data display, the system incorporates threshold-based abnormality detection. Each vital parameter is categorized into normal, warning, critical, or sensor fault states. The dashboard dynamically reflects these conditions, enabling quick identification of irregular physiological readings.

The architecture follows a modular and real-time oriented design approach, ensuring stable sensor sampling and uninterrupted communication. The result is a compact, low-cost, and extensible IoT platform suitable for academic research, prototyping, and further development toward wearable health monitoring solutions.
