// ============================================================
//  mac_printer.ino  —  Flash this FIRST on each ESP32
//  Open Serial Monitor at 115200 baud to see the MAC address.
//  Note down all 4 node MACs and fill them into config.h
// ============================================================
#include <WiFi.h>

void setup() {
    Serial.begin(115200);
    delay(1000);
    WiFi.mode(WIFI_STA);
    Serial.println("\n================================");
    Serial.println("  ESP32 MAC Address Printer");
    Serial.println("================================");
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.println("\nCopy this into config.h NODE_MACS[]");
    Serial.println("================================\n");
}

void loop() {
    // Print every 3 seconds in case you missed it
    delay(3000);
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
}
