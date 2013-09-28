/*
 Basic MQTT example 
 
  - connects to an MQTT server
  - publishes "hello world" to the topic "outTopic"
  - subscribes to the topic "inTopic"
*/

#include <Bridge.h>
#include "PubSubClient.h"

// Update these with values suitable for your network.
char * server = "192.168.0.2";

int ledPin = 13;

void callback(char* topic, byte* payload, unsigned int length) {
  // handle message arrived
  char incomingByte = payload[0];
  
  if (incomingByte == 'H') {
    digitalWrite(ledPin, HIGH);
  }
  
  if (incomingByte == 'L') {
    digitalWrite(ledPin, LOW);
  }
  
}

PubSubClient client(server, "1883", callback);

void setup()
{
  
  pinMode(ledPin, OUTPUT);
  
  Bridge.begin();
  
  digitalWrite(ledPin, HIGH);
  
  if (client.connect("a")) {
      
    client.publish("outTopic","hello world");
    client.subscribe("inTopic");
  }
}

void loop()
{
  client.loop();
}

