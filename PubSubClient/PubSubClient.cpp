/*
 PubSubClient.cpp - A simple client for MQTT.
  Nicholas O'Leary
  http://knolleary.net

  Matteo Collina
  http://matteocollina.com
*/

#include "PubSubClient.h"
#include <string.h>
#include <Process.h>

PubSubClient::PubSubClient() {
}

PubSubClient::PubSubClient(char* domain, char* port, void (*callback)(char*,uint8_t*,unsigned int)) {
   this->callback = callback;
   this->domain = domain;
   this->port = port;
}

boolean PubSubClient::connect(char *id) {
   return connect(id,NULL,NULL,0,0,0,0);
}

boolean PubSubClient::connect(char *id, char *user, char *pass) {
   return connect(id,user,pass,0,0,0,0);
}

boolean PubSubClient::connect(char *id, char* willTopic, uint8_t willQos, uint8_t willRetain, char* willMessage)
{
   return connect(id,NULL,NULL,willTopic,willQos,willRetain,willMessage);
}

boolean PubSubClient::connect(char *id, char *user, char *pass, char* willTopic, uint8_t willQos, uint8_t willRetain, char* willMessage) {
   if (!connected()) {
      
      this->launchProcess();
      
      nextMsgId = 1;
      uint8_t d[9] = {0x00,0x06,'M','Q','I','s','d','p',MQTTPROTOCOLVERSION};

      // Leave room in the buffer for header and variable length field
      uint16_t length = 5;
      unsigned int j;
      for (j = 0;j<9;j++) {
         buffer[length++] = d[j];
      }

      uint8_t v;
      if (willTopic) {
         v = 0x06|(willQos<<3)|(willRetain<<5);
      } else {
         v = 0x02;
      }

      if(user != NULL) {
         v = v|0x80;

         if(pass != NULL) {
            v = v|(0x80>>1);
         }
      }

      buffer[length++] = v;

      buffer[length++] = ((MQTT_KEEPALIVE) >> 8);
      buffer[length++] = ((MQTT_KEEPALIVE) & 0xFF);
      length = writeString(id,buffer,length);
      if (willTopic) {
         length = writeString(willTopic,buffer,length);
         length = writeString(willMessage,buffer,length);
      }

      if(user != NULL) {
         length = writeString(user,buffer,length);
         if(pass != NULL) {
            length = writeString(pass,buffer,length);
         }
      }
      
      
      write(MQTTCONNECT,buffer,length-5);
      
      lastInActivity = lastOutActivity = millis();
      
      while (!_process.available()) {
         unsigned long t = millis();
         if (t-lastInActivity > MQTT_KEEPALIVE*1000UL) {
            _process.close();
            return false;
         }
      }
      uint8_t llen;
      uint16_t len = readPacket(&llen);
      
      if (len == 4 && buffer[3] == 0) {
         lastInActivity = millis();
         pingOutstanding = false;
         return true;
      }
   }
   return false;
}

uint8_t PubSubClient::readByte() {
   while(!_process.available()) {}
   return _process.read();
}

uint16_t PubSubClient::readPacket(uint8_t* lengthLength) {
   uint16_t len = 0;
   buffer[len++] = readByte();
   uint8_t multiplier = 1;
   uint16_t length = 0;
   uint8_t digit = 0;
   do {
      digit = readByte();
      buffer[len++] = digit;
      length += (digit & 127) * multiplier;
      multiplier *= 128;
   } while ((digit & 128) != 0);
   *lengthLength = len-1;
   for (uint16_t i = 0;i<length;i++)
   {
      if (len < MQTT_MAX_PACKET_SIZE) {
         buffer[len++] = readByte();
      } else {
         readByte();
         len = 0; // This will cause the packet to be ignored.
      }
   }

   return len;
}

boolean PubSubClient::loop() {
   if (connected()) {
      unsigned long t = millis();
      if ((t - lastInActivity > MQTT_KEEPALIVE*1000UL) || (t - lastOutActivity > MQTT_KEEPALIVE*1000UL)) {
         if (pingOutstanding) {
            _process.close();
            return false;
         } else {
            buffer[0] = MQTTPINGREQ;
            buffer[1] = 0;
            _process.write(buffer[0]);
            _process.write(buffer[1]);
            lastOutActivity = t;
            lastInActivity = t;
            pingOutstanding = true;
         }
      }
      if (_process.available()) {
         uint8_t llen;
         uint16_t len = readPacket(&llen);
         if (len > 0) {
            lastInActivity = t;
            uint8_t type = buffer[0]&0xF0;
            if (type == MQTTPUBLISH) {
               if (callback) {
                  uint16_t tl = (buffer[llen+1]<<8)+buffer[llen+2];
                  char topic[tl+1];
                  for (uint16_t i=0;i<tl;i++) {
                     topic[i] = buffer[llen+3+i];
                  }
                  topic[tl] = 0;
                  // ignore msgID - only support QoS 0 subs
                  uint8_t *payload = buffer+llen+3+tl;
                  callback(topic,payload,len-llen-3-tl);
               }
            } else if (type == MQTTPINGREQ) {
               buffer[0] = MQTTPINGRESP;
               buffer[1] = 0;
               _process.write(buffer[0]);
               _process.write(buffer[1]);
            } else if (type == MQTTPINGRESP) {
               pingOutstanding = false;
            }
         }
      }
      return true;
   }
   return false;
}

boolean PubSubClient::publish(char* topic, char* payload) {
   return publish(topic,(uint8_t*)payload,strlen(payload),false);
}

boolean PubSubClient::publish(char* topic, uint8_t* payload, unsigned int plength) {
   return publish(topic, payload, plength, false);
}

boolean PubSubClient::publish(char* topic, uint8_t* payload, unsigned int plength, boolean retained) {
   if (connected()) {
      // Leave room in the buffer for header and variable length field
      uint16_t length = 5;
      length = writeString(topic,buffer,length);
      uint16_t i;
      for (i=0;i<plength;i++) {
         buffer[length++] = payload[i];
      }
      uint8_t header = MQTTPUBLISH;
      if (retained) {
         header |= 1;
      }
      return write(header,buffer,length-5);
   }
   return false;
}

boolean PubSubClient::publish_P(char* topic, uint8_t* PROGMEM payload, unsigned int plength, boolean retained) {
   uint8_t llen = 0;
   uint8_t digit;
   int rc;
   uint16_t tlen;
   int pos = 0;
   int i;
   uint8_t header;
   unsigned int len;
   
   if (!connected()) {
      return false;
   }
   
   tlen = strlen(topic);
   
   header = MQTTPUBLISH;
   if (retained) {
      header |= 1;
   }
   buffer[pos++] = header;
   len = plength + 2 + tlen;
   do {
      digit = len % 128;
      len = len / 128;
      if (len > 0) {
         digit |= 0x80;
      }
      buffer[pos++] = digit;
      llen++;
   } while(len>0);
   
   pos = writeString(topic,buffer,pos);
   
   for (i=0; i < pos; i++) {
     _process.write(buffer[i]);
   }
   rc += pos;
   
   for (i=0;i<plength;i++) {
      rc += _process.write((char)pgm_read_byte_near(payload + i));
   }
   
   lastOutActivity = millis();
   return rc == len + 1 + plength;
}

boolean PubSubClient::write(uint8_t header, uint8_t* buf, uint16_t length) {
   uint8_t lenBuf[4];
   uint8_t llen = 0;
   uint8_t digit;
   uint8_t pos = 0;
   uint8_t rc;
   uint8_t len = length;
   int i;
   
   do {
      digit = len % 128;
      len = len / 128;
      if (len > 0) {
         digit |= 0x80;
      }
      lenBuf[pos++] = digit;
      llen++;
   } while(len>0);

   buf[4-llen] = header;
   for (int i=0;i<llen;i++) {
      buf[5-llen+i] = lenBuf[i];
   }
   
   buf = buf + (4-llen);
   for (i= 0; i < length+1+llen; i++) {
     rc += _process.write(buf[i]);
   }
   
   lastOutActivity = millis();
   return (rc == 1+llen+length);
}

boolean PubSubClient::subscribe(char* topic) {
   if (connected()) {
      // Leave room in the buffer for header and variable length field
      uint16_t length = 5;
      nextMsgId++;
      if (nextMsgId == 0) {
         nextMsgId = 1;
      }
      buffer[length++] = (nextMsgId >> 8);
      buffer[length++] = (nextMsgId & 0xFF);
      length = writeString(topic, buffer,length);
      buffer[length++] = 0; // Only do QoS 0 subs
      return write(MQTTSUBSCRIBE|MQTTQOS1,buffer,length-5);
   }
   return false;
}

boolean PubSubClient::unsubscribe(char* topic) {
   if (connected()) {
      uint16_t length = 5;
      nextMsgId++;
      if (nextMsgId == 0) {
         nextMsgId = 1;
      }
      buffer[length++] = (nextMsgId >> 8);
      buffer[length++] = (nextMsgId & 0xFF);
      length = writeString(topic, buffer,length);
      return write(MQTTUNSUBSCRIBE|MQTTQOS1,buffer,length-5);
   }
   return false;
}

void PubSubClient::disconnect() {
   _process.write(MQTTDISCONNECT);
   _process.write(0);
   _process.close();
   lastInActivity = lastOutActivity = millis();
}

uint16_t PubSubClient::writeString(char* string, uint8_t* buf, uint16_t pos) {
   char* idp = string;
   uint16_t i = 0;
   pos += 2;
   while (*idp) {
      buf[pos++] = *idp++;
      i++;
   }
   buf[pos-i-2] = (i >> 8);
   buf[pos-i-1] = (i & 0xFF);
   return pos;
}


boolean PubSubClient::connected() {
   return _process.running();
}

void PubSubClient::launchProcess() {  
  this->_process.begin("nc");
  this->_process.addParameter(this->domain);
  this->_process.addParameter(this->port);
  this->_process.runAsynchronously();
}
