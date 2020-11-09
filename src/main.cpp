#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Update.h>
#include <esp_wifi.h>

AsyncWebServer server(80);
  
#define RELAY1 32
#define RELAY2 33
// #define BUTTON 16
#define PRESSED LOW
#define NOT_PRESSED HIGH
  
bool isAPmodeOn = false;
bool shouldReboot = false;
String value_login[3];
bool userFlag = false;
static bool eth_connected = false;

String IP_Address = "";
String networks = "";
String Delay1 = "10";
String Delay2 = "10";
String relay1 = "Relay1";
String relay2 = "Relay2";
String status1 = "OFF"; // barrier down
String status2 = "OFF"; // barrier down
String authentication = "";
bool needManualCloseRelayOne = false;
bool needManualCloseRelayTwo = false;
unsigned int startTimeRelayOne = 0;
unsigned int startTimeRelayTwo = 0;
unsigned int currentTimeRelayOne = 0;
unsigned int currentTimeRelayTwo = 0;

const char* passwordAP = "metrici@admin";
IPAddress local_IP_AP(109,108,112,114); //decimal for "mlpr"(metrici license plate recognition) in ASCII table
IPAddress gatewayAP(0,0,0,0);
IPAddress subnetAP(255,255,255,0);

const char* host_name = "metrici-relay-ethernet";

String strlog;

//------------------------- struct circular_buffer
struct ring_buffer
{
    ring_buffer(size_t cap) : buffer(cap) {}

    bool empty() const { return sz == 0 ; }
    bool full() const { return sz == buffer.size() ; }

    void push( String str )
    {
        if(last >= buffer.size()) last = 0 ;
        buffer[last] = str ;
        ++last ;
        if(full()) 
			first = (first+1) %  buffer.size() ;
        else ++sz ;
    }
    void print() const {
		strlog= "";
		if( first < last )
			for( size_t i = first ; i < last ; ++i ) {
				strlog += (buffer[i] + "<br>");
			}	
		else {
			for( size_t i = first ; i < buffer.size() ; ++i ) {
				strlog += (buffer[i] + "<br>");
			}
			for( size_t i = 0 ; i < last ; ++i ) {
				strlog += (buffer[i] + "<br>");
			}
		}
	}

    private:
        std::vector<String> buffer ;
        size_t first = 0 ;
        size_t last = 0 ;
        size_t sz = 0 ;
};
//------------------------- struct circular_buffer
ring_buffer circle(10);

//------------------------- logOutput(String)
void logOutput(String string1) {
	circle.push(string1);
	Serial.println(string1);
}
//------------------------- processor()
String processor(const String& var) {
	circle.print();
	if  (var == "PLACEHOLDER_LOGS")
		return strlog;
  else if (var == "PLACEHOLDER_Timer1")
    return Delay1;
  else if (var == "PLACEHOLDER_Timer2")
    return Delay2;
  else if (var == "PLACEHOLDER_Relay1")
    return relay1;
  else if (var == "PLACEHOLDER_Relay2")
    return relay2;
  else if (var == "PLACEHOLDER_Status1")
    return status1;
  else if (var == "PLACEHOLDER_Status2")
    return status2;
  else if (var == "PH_Auth")
    return authentication;
  else if (var == "PH_IP_Addr")
    return IP_Address;
  else if (var == "PLACEHOLDER_NETWORKS")
    return networks;
  else if (var == "PH_Version")
    return String("v1.5");
	return String();  
}

//------------------------- listAllFiles()
void listAllFiles() {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while(file){
    Serial.print("FILE: ");
    String fileName = file.name();
    Serial.println(fileName);
    file = root.openNextFile();
  }
  file.close();
  root.close();
}

//------------------------- fileReadLines()
void fileReadLines(File file, String x[]) {
  int i = 0;
    while(file.available()){
      String line= file.readStringUntil('\n');
      line.trim();
      x[i] = line;
      i++;
      // logOutput((String)"Line: " + line);
    }
}

String readString(File s) {
  // Read from file witout yield 
  String ret;
  int c = s.read();
  while (c >= 0) {
    ret += (char)c;
    c = s.read();
  }
  return ret;
}

// Add files to the /files table
String& addDirList(String& HTML) {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  int count = 0;
  while (file) {
    String fileName = file.name();
    File dirFile = SPIFFS.open(fileName, "r");
    size_t filelen = dirFile.size();
    dirFile.close();
    String filename_temp = fileName;
    filename_temp.replace("/", "");
    String directories = "<form action=\"/files\" method=\"POST\">\r\n<tr><td align=\"center\">";
    if(fileName.indexOf("Relay") > 0) {
      directories += "<input type=\"hidden\" name = \"filename\" value=\"" + fileName + "\">" + filename_temp;
      directories += "</td>\r\n<td align=\"center\">" + String(filelen, DEC);
      directories += "</td><td align=\"center\"><button style=\"margin-right:20px;\" type=\"submit\" name= \"download\">Download</button><button type=\"submit\" name= \"delete\">Delete</button></td>\r\n";
      directories += "</tr></form>\r\n~directories~";
      HTML.replace("~directories~", directories);
      count++;
    }
    file = root.openNextFile();
  }
  HTML.replace("~directories~", "");
  
  HTML.replace("~count~", String(count, DEC));
  HTML.replace("~total~", String(SPIFFS.totalBytes() / 1024, DEC));
  HTML.replace("~used~", String(SPIFFS.usedBytes() / 1024, DEC));
  HTML.replace("~free~", String((SPIFFS.totalBytes() - SPIFFS.usedBytes()) / 1024, DEC));

  return HTML;
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  if(filename.indexOf(".bin") > 0) {
    if (!index){
      logOutput("The update process has started...");
      // if filename includes spiffs, update the spiffs partition
      int cmd = (filename.indexOf("spiffs") > -1) ? U_SPIFFS : U_FLASH;
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
        Update.printError(Serial);
      }
    }

    if (Update.write(data, len) != len) {
      Update.printError(Serial);
    }

    if (final) {
      if (filename.indexOf("spiffs") > -1) {
        request->send(200, "text/html", "<div style=\"margin:0 auto; text-align:center; font-family:arial;\">The device entered AP Mode ! Please connect to it.</div>");  
      } else {
        request->send(200, "text/html", "<div style=\"margin:0 auto; text-align:center; font-family:arial;\">Congratulation ! </br> You have successfully updated the device to the latest version. </br>Please wait 10 seconds until the device reboots, then press on the \"Go Home\" button to go back to the main page.</br></br> <form method=\"post\" action=\"http://" + IP_Address + "\"><input type=\"submit\" name=\"goHome\" value=\"Go Home\"/></form></div>");
      }

      if (!Update.end(true)){
        Update.printError(Serial);
      } else {
        logOutput("Update complete");
        Serial.flush();
        ESP.restart();
      }
    }
  } else {
      if(!index){
        logOutput((String)"Started uploading: " + filename);
        // open the file on first call and store the file handle in the request object
        request->_tempFile = SPIFFS.open("/"+filename, "w");
      }
      if(len) {
        // stream the incoming chunk to the opened file
        request->_tempFile.write(data,len);
      }
      if(final){
        logOutput((String)filename + " was successfully uploaded! File size: " + index+len);
        // close the file handle as the upload is now done
        request->_tempFile.close();
        request->redirect("/files");
      }
    }
}

//------------------------- void EthernetConfig(String x[])
void EthernetConfig(String x[]){
  if(x[0] == "WiFi") return;
  if(x[1] != NULL &&      //Local IP
     x[1].length() != 0 &&
     x[2] != NULL &&      // Gateway
     x[2].length() != 0 &&
     x[3] != NULL &&      // Subnet
     x[3].length() != 0 &&
     x[4] != NULL &&      // DNS
     x[4].length() != 0) {
       IPAddress local_IP_STA, gateway_STA, subnet_STA, primaryDNS;
       local_IP_STA.fromString(x[1]);
       gateway_STA.fromString(x[2]);
       subnet_STA.fromString(x[3]);
       primaryDNS.fromString(x[4]);
       if(!ETH.config(local_IP_STA, gateway_STA, subnet_STA, primaryDNS)) {
         logOutput((String)"Couldn't configure STATIC IP ! Obtaining DHCP IP !");
       }
       delay(50);
  } else {
    logOutput((String)"Obtaining DHCP IP !");
  }
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
      ETH.setHostname(host_name);
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      eth_connected = true;
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
}

//------------------------- startWiFiSTA()
void startWiFiSTA(String x[]){
  if(x[0] == "Ethernet") return;
  WiFi.mode(WIFI_MODE_STA);
  if(x[1] != NULL &&      // SSID
     x[1].length() != 0 &&
     x[2] != NULL &&      // Password
     x[2].length() != 0 &&
     x[3] != NULL &&      //Local IP
     x[3].length() != 0 &&
     x[4] != NULL &&      // Gateway
     x[4].length() != 0 &&
     x[5] != NULL &&      // Subnet
     x[5].length() != 0 &&
     x[6] != NULL &&      // DNS
     x[6].length() != 0) {
       IPAddress local_IP_STA, gateway_STA, subnet_STA, primaryDNS;
       local_IP_STA.fromString(x[3]);
       gateway_STA.fromString(x[4]);
       subnet_STA.fromString(x[5]);
       primaryDNS.fromString(x[6]);
       if(!WiFi.config(local_IP_STA, gateway_STA, subnet_STA, primaryDNS)) {
         logOutput((String)"Couldn't configure STATIC IP ! Starting DHCP IP !");
       }
       delay(50);
       WiFi.begin(x[1].c_str(),x[2].c_str());
  } else if(x[1] != NULL &&           // SSID
            x[1].length() != 0 &&
            x[2] != NULL &&           // Password
            x[2].length() != 0 &&
            x[3] == NULL &&           //Local IP
            x[3].length() == 0 &&
            x[4] == NULL &&           // Gateway
            x[4].length() == 0 &&
            x[5] == NULL &&           // Subnet
            x[5].length() == 0 &&
            x[6] == NULL &&           // DNS
            x[6].length() == 0) {
              WiFi.begin(x[1].c_str(),x[2].c_str());
        }

  int k = 0;
  while (WiFi.status() != WL_CONNECTED && k<20) {
    k++;
    delay(1000);
    logOutput((String)"Attempt " + k + " - Connecting to WiFi..");
  }
}

//------------------------- startWiFiAP()
void startWiFiAP() {  
  delay(50);
  if(SPIFFS.exists("/networkRelay.txt")) SPIFFS.remove("/networkRelay.txt");
  

  String x = "Metrici";
  x.concat(random(100,999));
  char ssidAP[x.length()+1];
  x.toCharArray(ssidAP,sizeof(ssidAP));

  logOutput((String)"Starting AP ... ");
  logOutput(WiFi.softAP(ssidAP, passwordAP) ? (String) ssidAP + " ready" : "WiFi.softAP failed ! (Password must be at least 8 characters long )");
  delay(500);
  logOutput((String)"Setting AP configuration ... ");
  logOutput(WiFi.softAPConfig(local_IP_AP, gatewayAP, subnetAP) ? "Ready" : "Failed!");
  delay(500);
  logOutput((String)"Soft-AP IP address: ");
  logOutput(WiFi.softAPIP().toString());
  // digitalWrite(LED, HIGH);
  delay(500);

  
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/favicon.ico", "image/ico");
  });
  server.on("/register", HTTP_GET, [](AsyncWebServerRequest *request){
    // request->send(SPIFFS, "/indexAP.html", "text/html", false, processor);
    AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/indexAP.html", "text/html", false, processor);
    File file = SPIFFS.open("/indexAP.html");
    response->addHeader("Content-Length", (String) file.size ());
    request->send(response);
    file.close();
  });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->redirect("/register");
  });
  server.on("/IP-Config", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/AP_configPage.html", "text/html", false, processor);
  });
  server.on("/networks_placeholders.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/networks_placeholders.html", "text/html", false, processor);
  });
  server.on("/logs", HTTP_GET, [](AsyncWebServerRequest* request){
    request->send(SPIFFS, "/events.html", "text/html", false, processor);
  });
  server.on("/events_placeholder.html", HTTP_GET, [](AsyncWebServerRequest* request){
    request->send(SPIFFS, "/events_placeholder.html", "text/html", false, processor);
  });	    
  server.on("/dhcpIP", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/dhcpIP.html", "text/html", false, processor);
  });
  server.on("/staticIP", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/staticIP.html", "text/html", false, processor);
  });     
  server.on("/newMaster.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/newMaster.css", "text/css");
  });
  server.on("/jquery-1.12.4.min.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/jquery-1.12.4.min.js", "text/javascript");
  });
  server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/logo.png", "image/png");
  });

  server.on("/register", HTTP_POST, [](AsyncWebServerRequest * request){
    if(request->hasArg("register")){
      int params = request->params();
      String values_user[params];
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
            logOutput((String)"POST[" + p->name().c_str() + "]: " + p->value().c_str() + "\n");            
            values_user[i] = p->value();            
          } else {
              logOutput((String)"GET[" + p->name().c_str() + "]: " + p->value().c_str() + "\n");
            }
      } // for(int i=0;i<params;i++)
      
      if(values_user[0] != NULL && values_user[0].length() > 4 &&
        values_user[1] != NULL && values_user[1].length() > 7) {
            File userWrite = SPIFFS.open("/userRelay.txt", "w");
            if(!userWrite) logOutput((String)"ERROR_INSIDE_POST ! Couldn't open file to write USER credentials !");
            userWrite.println(values_user[0]);  // Username
            userWrite.println(values_user[1]);  // Password
            userWrite.close();
            logOutput("Username and password saved !");          
            request->redirect("/IP-Config");
            // digitalWrite(LED, LOW);
      } else request->redirect("/register");  
    } else if (request->hasArg("skip")) {
      request->redirect("/IP-Config");
    } else if(request->hasArg("import")) {
      request->redirect("/files");
    } else {
      request->redirect("/register");
    }
  }); // server.on("/register", HTTP_POST, [](AsyncWebServerRequest * request)

  server.on("/files", HTTP_ANY, [](AsyncWebServerRequest *request){
    // Serial.print("/files, request: ");
    // Serial.println(request->methodToString());

    if (request->hasParam("filename", true)) { // Check for files
      if (request->hasArg("download")) { // Download file
        Serial.println("Download Filename: " + request->arg("filename"));
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, request->arg("filename"), String(), true);
        response->addHeader("Server", "ESP Async Web Server");
        request->send(response);
        return;
      } else if(request->hasArg("delete")) { // Delete file
        if (SPIFFS.remove(request->getParam("filename", true)->value())) {
          logOutput((String)request->getParam("filename", true)->value().c_str() + " was deleted !");
        } else {
          logOutput("Could not delete file. Try again !");
        }
        request->redirect("/files");
      }
    } else if(request->hasArg("goBack")) { // Go Back Button
      request->redirect("register");
    } else if(request->hasArg("restart_device")) {
        request->send(200,"text/plain", "The device will reboot shortly !");
        ESP.restart();
    }

    String HTML PROGMEM; // HTML code 
    String filename = request->url() + ".html";
    File pageFile = SPIFFS.open(filename, "r");
    if (pageFile) {
      HTML = readString(pageFile);
      pageFile.close();
      HTML = addDirList(HTML);
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML.c_str(), processor);
      response->addHeader("Server","ESP Async Web Server");
      request->send(response);
    }
  });

  server.on("/staticIP", HTTP_POST, [](AsyncWebServerRequest * request){
    if(request->hasArg("saveStatic")){
      int params = request->params();
      String values_static_post[params];
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
            logOutput((String)"POST[" + p->name() + "]: " + p->value());
            values_static_post[i] = p->value();
          } else {
              logOutput((String)"GET[" + p->name() + "]: " + p->value());
            }
      } // for(int i=0;i<params;i++)

        if(values_static_post[0] == "WiFi") {
          if(values_static_post[1] != NULL &&
          values_static_post[1].length() != 0 &&
          values_static_post[2] != NULL &&
          values_static_post[2].length() != 0 &&
          values_static_post[3] != NULL &&
          values_static_post[3].length() != 0 &&
          values_static_post[4] != NULL &&
          values_static_post[4].length() != 0 &&
          values_static_post[5] != NULL &&
          values_static_post[5].length() != 0 &&
          values_static_post[6] != NULL &&
          values_static_post[6].length() != 0) {
              File inputsWrite = SPIFFS.open("/networkRelay.txt", "w");
              if(!inputsWrite) logOutput((String)"ERROR_INSIDE_POST ! Couldn't open file to write Static IP credentials !"); 
              inputsWrite.println(values_static_post[0]);   // Connection Type ? WiFi : Ethernet
              inputsWrite.println(values_static_post[1]);   // SSID
              inputsWrite.println(values_static_post[2]);   // Password
              inputsWrite.println(values_static_post[3]);   // Local IP
              inputsWrite.println(values_static_post[4]);   // Gateway
              inputsWrite.println(values_static_post[5]);   // Subnet
              inputsWrite.println(values_static_post[6]);   // DNS
              inputsWrite.close();
              logOutput("Configuration saved !");
              request->redirect("/logs");
              shouldReboot = true;
        } else request->redirect("/staticIP");
      } else if (values_static_post[0] == "Ethernet") {
          if(values_static_post[3] != NULL &&
            values_static_post[3].length() != 0 &&
            values_static_post[4] != NULL &&
            values_static_post[4].length() != 0 &&
            values_static_post[5] != NULL &&
            values_static_post[5].length() != 0 &&
            values_static_post[6] != NULL &&
            values_static_post[6].length() != 0) {
              File inputsWrite = SPIFFS.open("/networkRelay.txt", "w");
              if(!inputsWrite) logOutput((String)"ERROR_INSIDE_POST ! Couldn't open file to write Static IP credentials !"); 
              inputsWrite.println(values_static_post[0]);   // Connection Type ? WiFi : Ethernet
              inputsWrite.println(values_static_post[3]);   // Local IP
              inputsWrite.println(values_static_post[4]);   // Gateway
              inputsWrite.println(values_static_post[5]);   // Subnet
              inputsWrite.println(values_static_post[6]);   // DNS
              inputsWrite.close();
              logOutput("Configuration saved !");
              request->redirect("/logs");
              shouldReboot = true;
        } else request->redirect("/staticIP");
      }// if(values_static_post[0] == "WiFi")
    } else {
      request->redirect("/staticIP");
    } // if(request->hasArg("saveStatic"))

  }); // server.on("/staticLogin", HTTP_POST, [](AsyncWebServerRequest * request)

  server.on("/dhcpIP", HTTP_POST, [](AsyncWebServerRequest * request){
    if(request->hasArg("saveDHCP")){
      int params = request->params();
      // Serial.println((String)"**DEBUG** I have " + params + " params on this page");
      String values_dhcp_post[params];
      
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
            logOutput((String)"POST[" + p->name() + "]: " + p->value());            
            values_dhcp_post[i] = p->value();
          } else {
              logOutput((String)"GET[" + p->name() + "]: " + p->value());              
            }
      }

      if(values_dhcp_post[0] == "WiFi") {
        if(values_dhcp_post[1] != NULL && values_dhcp_post[1].length() != 0 &&
          values_dhcp_post[2] != NULL && values_dhcp_post[2].length() != 0) {
          File inputsWrite = SPIFFS.open("/networkRelay.txt", "w");
          if(!inputsWrite) logOutput((String)"ERROR_INSIDE_POST ! Couldn't open file to write DHCP IP credentials !");
          inputsWrite.println(values_dhcp_post[0]);  // Connection Type ? WiFi : Ethernet
          inputsWrite.println(values_dhcp_post[1]);  // SSID
          inputsWrite.println(values_dhcp_post[2]);  // Password
          inputsWrite.close();
          logOutput("Configuration saved !");
          request->redirect("/logs");
          shouldReboot = true;
        } else request->redirect("/dhcpIP");
      } else if (values_dhcp_post[0] == "Ethernet") {
          File inputsWrite = SPIFFS.open("/networkRelay.txt", "w");
          if(!inputsWrite) logOutput((String)"ERROR_INSIDE_POST ! Couldn't open file to write DHCP IP credentials !");
          inputsWrite.println(values_dhcp_post[0]);  // Connection Type ? WiFi : Ethernet
          inputsWrite.close();
          logOutput("Configuration saved !");
          request->redirect("/logs");
          shouldReboot = true;
      }
    } else {
      request->redirect("/dhcpIP");
    } // if(request->hasArg("saveStatic"))  
  }); // server.on("/dhcpLogin", HTTP_POST, [](AsyncWebServerRequest * request)
  
  server.onFileUpload(handleUpload);
  server.onNotFound([](AsyncWebServerRequest *request){
    request->redirect("/register");
  });

  server.begin(); //-------------------------------------------------------------- server.begin()

} // void startWiFiAP()

void setup(){
  enableCore1WDT();
  delay(100);
  Serial.begin(115200);
  delay(100);

  WiFi.onEvent(WiFiEvent);

  pinMode(RELAY1, OUTPUT);
  digitalWrite(RELAY1, LOW);
  pinMode(RELAY2, OUTPUT);
  digitalWrite(RELAY2, LOW);
  // pinMode(BUTTON,INPUT_PULLUP);
  // digitalWrite(BUTTON, NOT_PRESSED);

  // int q = 0;
  // if (digitalRead(BUTTON) == PRESSED) {
  //   q++;
  //   delay(2000);
  //   if (digitalRead(BUTTON) == PRESSED) {
  //     q++;
  //   }
  // }
  // Serial.println((String)"Q == " + q);

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS ! Formatting in progress");
    return;
  }

  // if (q == 2) {
  //   delay(10);
  //   //--- Blink 2 times
  //   // digitalWrite(LED, HIGH); delay(200); digitalWrite(LED, LOW); delay(200); digitalWrite(LED, HIGH); delay(200); digitalWrite(LED, LOW); delay(10);
  //   if(SPIFFS.exists("/networkRelay.txt")) SPIFFS.remove("/networkRelay.txt");
  //   if(SPIFFS.exists("/userRelay.txt")) SPIFFS.remove("/userRelay.txt");
  //   if(SPIFFS.exists("/configRelay.txt")) SPIFFS.remove("/configRelay.txt");
  //   logOutput((String)"Reset button was pressed ! AP will start momentarily");
  // }

  listAllFiles();

  //--- Check if /networkRelay.txt exists. If not then create one
  if(!SPIFFS.exists("/networkRelay.txt")) {
    File inputsCreate = SPIFFS.open("/networkRelay.txt", "w");
    if(!inputsCreate) logOutput((String)"Couldn't create /networkRelay.txt");
    logOutput("Was /networkRelay.txt created ?");
    logOutput(SPIFFS.exists("/networkRelay.txt") ? "Yes" : "No");
    inputsCreate.close();
  }
  
  //--- Check if /networkRelay.txt is empty (0 bytes) or not (>8)
  //--- println adds /r and /n, which means 2 bytes
  //--- 2 lines = 2 println = 4 bytes
  File networkRead = SPIFFS.open("/networkRelay.txt");
  if(!networkRead) logOutput((String)"ERROR ! Couldn't open file to read !");

  if(networkRead.size() > 8) {
    //--- Read SSID, Password, Local IP, Gateway, Subnet, DNS from file
    //--- and store them in v[]
    String v[7];
    fileReadLines(networkRead,v);
    networkRead.close();
    if(v[0] == "WiFi") { // WiFi ?      
      esp_wifi_set_ps (WIFI_PS_NONE);
      //--- Start ESP in Station Mode using the above credentials
      startWiFiSTA(v);
      if(WiFi.status() != WL_CONNECTED) {
        logOutput((String)"(1) Could not access Wireless Network ! Trying again...");
        logOutput((String)"Controller will restart in 5 seconds !");
        delay(5000);
        ESP.restart();
      }
      IP_Address = WiFi.localIP().toString();
      logOutput((String)"Connected to " + v[1] + " with IP addres: " + IP_Address);
      logOutput((String)"Gateway: " + WiFi.gatewayIP().toString());
      logOutput((String)"Subnet: " + WiFi.subnetMask().toString());
      logOutput((String)"DNS: " + WiFi.dnsIP().toString());      
    } else if(v[0] == "Ethernet") { // Ethernet ?
      ETH.begin();
      int ki = 0;
      while(!eth_connected && ki <20) {
        Serial.println("Establishing ETHERNET Connection ... ");
        delay(1000);
        ki++;
      }
      if(!eth_connected) {
        logOutput((String)"(1) Could not access Network ! Trying again...");
        logOutput((String)"Controller will restart in 5 seconds !");
        delay(5000);
        ESP.restart();
      }
      EthernetConfig(v);
      IP_Address = ETH.localIP().toString();
      logOutput((String)"IP addres: " + IP_Address);
      logOutput((String)"Gateway: " + ETH.gatewayIP().toString());
      logOutput((String)"Subnet: " + ETH.subnetMask().toString());
      logOutput((String)"DNS: " + ETH.dnsIP().toString());
    } // if(v[0] == "WiFi")
    
    if(SPIFFS.exists("/userRelay.txt")) {
      File userRead = SPIFFS.open("/userRelay.txt", "r");
      if(!userRead) logOutput((String)"Couldn't read /userRelay.txt");
      fileReadLines(userRead, value_login);
      if(value_login[0].length() >4 && value_login[0] != NULL &&
          value_login[1].length() >7 && value_login[1] != NULL) 
          {
          userFlag = true;
          authentication = String(value_login[0] + ":" + value_login[1] + "@");
          }
    }

    if(SPIFFS.exists("/configRelay.txt")) {
      String values_config[4];
      File configRead = SPIFFS.open("/configRelay.txt", "r");
      if(!configRead) logOutput((String)"ERROR_INSIDE_CONFIG_FILE_READ ! Couldn't open /configFile to read values!");
      fileReadLines(configRead, values_config);          
      if (values_config[0]!= NULL || values_config[0].length() != 0) {
        relay1 = values_config[0];
      } else {
        values_config[0] = "Default Relay 1 Name: " + relay1;
      }
      if (values_config[1]!= NULL || values_config[1].length() != 0) {
        Delay1 = values_config[1];
      } else {
        values_config[1] = "Default Timer 1: " + Delay1;
      }
      if (values_config[2]!= NULL || values_config[2].length() != 0) {
        relay2 = values_config[2];
      } else {
        values_config[2] = "Default Relay 2 Name: " + relay2;
      }
      if (values_config[3]!= NULL || values_config[3].length() != 0) {
        Delay2 = values_config[3];
      } else {
        values_config[3] = "Default Timer 2: " + Delay2;
      }
      logOutput("Present Configuration: ");
      for(int k=0; k<4; k++) {
        logOutput((String)(k+1) + ": " + values_config[k]);
      }
      configRead.close();
    }
  
  server.on("/relay1/on", HTTP_GET, [](AsyncWebServerRequest *request){
    if(userFlag) {
      if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
        return request->requestAuthentication(NULL,false);    
      digitalWrite(RELAY1, HIGH);
      status1 = "ON";
      logOutput((String)relay1 + " is ON");
      if(Delay1.toInt() == 0) {
        needManualCloseRelayOne = true;
        logOutput((String)relay1 + " will remain open until it is manually closed !");
      } else {
        needManualCloseRelayOne = false;
        logOutput((String)relay1 + " will automatically close in " + Delay1 + " seconds !");
      }          
      startTimeRelayOne = millis();
      // Serial.println("Do I get here ? /relay1/on");
      request->send(200, "text/plain", relay1 + " is ON");
    } else {    
        digitalWrite(RELAY1, HIGH);
        status1 = "ON";
        logOutput((String)relay1 + " is ON");
        if(Delay1.toInt() == 0) {
          needManualCloseRelayOne = true;
          logOutput((String)relay1 + " will remain open until it is manually closed !");
        } else {
          needManualCloseRelayOne = false;
          logOutput((String)relay1 + " will automatically close in " + Delay1 + " seconds !");
        }
        startTimeRelayOne = millis();
        // Serial.println("Do I get here ? /relay1/on");
        request->send(200, "text/plain", relay1 + " is ON");
    }
  });

  server.on("/relay1/off", HTTP_GET, [](AsyncWebServerRequest *request){
    if(userFlag) {
      if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
        return request->requestAuthentication(NULL,false);    
      digitalWrite(RELAY1, LOW);
      status1 = "OFF";
      logOutput((String)relay1 + " is OFF");
      needManualCloseRelayOne = true;
      // Serial.println("Do I get here ? /relay1/off");
      request->send(200, "text/plain", relay1 + " is OFF");
    } else {    
        digitalWrite(RELAY1, LOW);
        status1 = "OFF";
        logOutput((String)relay1 + " is OFF");
        needManualCloseRelayOne = true;
        // Serial.println("Do I get here ? /relay1/off");
        request->send(200, "text/plain", relay1 + " is OFF");
    }
  });

  server.on("/relay2/on", HTTP_GET, [](AsyncWebServerRequest *request){
    if(userFlag) {
      if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
        return request->requestAuthentication(NULL,false);
      digitalWrite(RELAY2, HIGH);
      status2 = "ON";
      logOutput((String)relay2 + " is ON");
      if(Delay2.toInt() == 0) {
        needManualCloseRelayTwo = true;
          logOutput((String)relay2 + " will remain open until it is manually closed !");
      } else {
        needManualCloseRelayTwo = false;
        logOutput((String)relay2 + " will automatically close in " + Delay2 + " seconds !");
      }          
      startTimeRelayTwo = millis();
      // Serial.println("Do I get here ? /relay2/on");
      request->send(200, "text/plain", relay2 + " is ON");
    } else {
        digitalWrite(RELAY2, HIGH);
        status2 = "ON";
        logOutput((String)relay2 + " is ON");
        if(Delay2.toInt() == 0) {
          needManualCloseRelayTwo = true;
          logOutput((String)relay2 + " will remain open until it is manually closed !");
        } else {
          needManualCloseRelayTwo = false;
          logOutput((String)relay2 + " will automatically close in " + Delay2 + " seconds !");
        }      
        startTimeRelayTwo = millis();
        // Serial.println("Do I get here ? /relay2/on");
        request->send(200, "text/plain", relay2 + " is ON");
    }
  });

  server.on("/relay2/off", HTTP_GET, [](AsyncWebServerRequest *request){
    if(userFlag) {
      if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
        return request->requestAuthentication(NULL,false);
      digitalWrite(RELAY2, LOW);
      status2 = "OFF";
      logOutput((String)relay2 + " is OFF");
      needManualCloseRelayTwo = true;
      // Serial.println("Do I get here ? /relay2/off");
      request->send(200, "text/plain", relay2 + " is OFF");
    } else {
        digitalWrite(RELAY2, LOW);
        status2 = "OFF";
        logOutput((String)relay2 + " is OFF");
        needManualCloseRelayTwo = true;
        // Serial.println("Do I get here ? /relay2/off");
        request->send(200, "text/plain", relay2 + " is OFF");
    }
  });
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/favicon.ico", "image/ico");
  });
  server.on("/events_placeholder.html", HTTP_GET, [](AsyncWebServerRequest* request){
    request->send(SPIFFS, "/events_placeholder.html", "text/html", false, processor);
  });
  server.on("/relay_status1.html", HTTP_GET, [](AsyncWebServerRequest* request){
    request->send(SPIFFS, "/relay_status1.html", "text/html", false, processor);
  });
  server.on("/relay_status2.html", HTTP_GET, [](AsyncWebServerRequest* request){
    request->send(SPIFFS, "/relay_status2.html", "text/html", false, processor);
  });

  server.on("/newMaster.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/newMaster.css", "text/css");
  });
  server.on("/jquery-1.12.4.min.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/jquery-1.12.4.min.js", "text/javascript");
  });
  server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/logo.png", "image/png");
  });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->redirect("/home");
  });

  server.on("/home", HTTP_GET, [](AsyncWebServerRequest* request){
    if(userFlag) {
      if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
        return request->requestAuthentication(NULL,false);
      // request->send(SPIFFS, "/index.html", "text/html", false, processor);
        AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/index.html", "text/html", false, processor);
        File file = SPIFFS.open("/index.html");
        response->addHeader("Content-Length", (String) file.size ());
        request->send(response);
        file.close();
    } else {          
      // request->send(SPIFFS, "/index.html", "text/html", false, processor);
      AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/index.html", "text/html", false, processor);
      File file = SPIFFS.open("/index.html");
      response->addHeader("Content-Length", (String) file.size ());
      request->send(response);
      file.close();
    }        
  });

  server.on("/IP-Config", HTTP_GET, [](AsyncWebServerRequest *request){
    if(userFlag) {
      if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
        return request->requestAuthentication(NULL,false);            
      request->send(SPIFFS, "/AP_configPage.html", "text/html");
    } else {
      request->send(SPIFFS, "/AP_configPage.html", "text/html");
    }  
  });
  server.on("/dhcpIP", HTTP_GET, [](AsyncWebServerRequest *request){
    if(userFlag) {
      if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
        return request->requestAuthentication(NULL,false);            
      request->send(SPIFFS, "/dhcpIP.html", "text/html");
    } else {
      request->send(SPIFFS, "/dhcpIP.html", "text/html");
    }
  });
  server.on("/staticIP", HTTP_GET, [](AsyncWebServerRequest *request){
    if(userFlag) {
      if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
        return request->requestAuthentication(NULL,false);            
      request->send(SPIFFS, "/staticIP.html", "text/html");
    } else {
      request->send(SPIFFS, "/staticIP.html", "text/html");
    }
  });

  server.on("/home", HTTP_POST, [](AsyncWebServerRequest * request){
    if(digitalRead(RELAY1)) {
      status1 = "ON";
    } else {
      status1 = "OFF";
    }
    if(digitalRead(RELAY2)) {
      status2 = "ON";
    } else {
      status2 = "OFF";
    }
    if(request->hasArg("save_values")){
      request->redirect("/home");
      int params = request->params();
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost() && p->value() != NULL && p->name() != "save_values") {
          File configWrite = SPIFFS.open("/configRelay.txt", "w");
          if(!configWrite) logOutput((String)"ERROR_INSIDE_DELAY_POST ! Couldn't open file to save DELAY !");
          logOutput((String)"POST[" + p->name() + "]: " + p->value());
          if(p->name() == "getDelay1") {
            Delay1 = p->value();
            configWrite.println(relay1);
            configWrite.println(Delay1);
            configWrite.println(relay2);
            configWrite.println(Delay2);
          }
          if(p->name() == "getDelay2") {
            Delay2 = p->value();
            configWrite.println(relay1);
            configWrite.println(Delay1);
            configWrite.println(relay2);
            configWrite.println(Delay2);
          }
          if(p->name() == "getRelay1") {
            relay1 = p->value();
            configWrite.println(relay1);
            configWrite.println(Delay1);
            configWrite.println(relay2);
            configWrite.println(Delay2);
          }
          if(p->name() == "getRelay2") {
            relay2 = p->value();
            configWrite.println(relay1);
            configWrite.println(Delay1);
            configWrite.println(relay2);
            configWrite.println(Delay2);
          }
          configWrite.close();
        }
        //  else {
        //     logOutput((String)"GET[" + p->name().c_str() + "]: " + p->value().c_str());
        //   }
      } // for(int i=0;i<params;i++)
    } else if(request->hasArg("goUpdate")) {
      request->redirect("/update");
    } else if(request->hasArg("relay1_on")) {
        Serial.println("relay1 /on button was PRESSED");
        digitalWrite(RELAY1, HIGH);
        status1 = "ON";
        logOutput((String)relay1 + " is ON");
        if(Delay1.toInt() == 0) {
          needManualCloseRelayOne = true;
          logOutput((String)relay1 + " will remain open until it is manually closed !");
        } else {
          needManualCloseRelayOne = false;
          logOutput((String)relay1 + " will automatically close in " + Delay1 + " seconds !");
        }
        startTimeRelayOne = millis();
        request->redirect("/home");
    } else if(request->hasArg("relay1_off")) {
        Serial.println("relay1 /off button was PRESSED");
        logOutput((String)"You have manually closed " + relay1);
        digitalWrite(RELAY1, LOW);
        status1 = "OFF";
        needManualCloseRelayOne = true; // I pressed the /off button and the timer won't start
        logOutput((String)relay1 + " is OFF");
        request->redirect("/home");
    } else if(request->hasArg("relay2_on")) {
        Serial.println("relay2 /on button was PRESSED");
        digitalWrite(RELAY2, HIGH);
        status2 = "ON";
        logOutput((String)relay2 + " is ON");
        if(Delay2.toInt() == 0) {
          needManualCloseRelayTwo = true;
          logOutput((String)relay2 + " will remain open until it is manually closed !");
        } else {
          needManualCloseRelayTwo = false;
          logOutput((String)relay2 + " will automatically close in " + Delay2 + " seconds !");
        }
        startTimeRelayTwo = millis();
        request->redirect("/home");
    } else if(request->hasArg("relay2_off")) {
        Serial.println("relay2 /off button was PRESSED");          
        logOutput((String)"You have manually closed " + relay2);
        digitalWrite(RELAY2, LOW);
        status2 = "OFF";
        needManualCloseRelayTwo = true; // I pressed the button and the timer won't start
        logOutput((String)relay2 + " is OFF");
        request->redirect("/home");
    } else if(request->hasArg("ip_settings")) {
        request->redirect("/IP-Config");
    } else if(request->hasArg("import_export")) {
        request->redirect("/files");
    } else {
      request->redirect("/home");
    }
  });

  server.on("/files", HTTP_ANY, [](AsyncWebServerRequest *request){
    // Serial.print("/files, request: ");stop
    // Serial.println(request->methodToString());
    if(userFlag) {
      if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
        return request->requestAuthentication(NULL,false);            
      if (request->hasParam("filename", true)) { // Download file
        if (request->hasArg("download")) { // File download
          Serial.println("Download Filename: " + request->arg("filename"));
          AsyncWebServerResponse *response = request->beginResponse(SPIFFS, request->arg("filename"), String(), true);
          response->addHeader("Server", "ESP Async Web Server");
          request->send(response);
          return;
        } else if(request->hasArg("delete")) { // Delete file
          if (SPIFFS.remove(request->getParam("filename", true)->value())) {
            logOutput((String)request->getParam("filename", true)->value().c_str() + " was deleted !");
          } else {
            logOutput("Could not delete file. Try again !");
          }
          request->redirect("/files");
        }
      } else if(request->hasArg("goBack")) { // Go Back Button
        request->redirect("register");
      } else if(request->hasArg("restart_device")) {
        request->send(200,"text/plain", "The device will reboot shortly !");
        ESP.restart();
      }
      String HTML PROGMEM; // HTML code 
      String filename = request->url() + ".html";
      File pageFile = SPIFFS.open(filename, "r");
      if (pageFile) {
        HTML = readString(pageFile);
        pageFile.close();
        HTML = addDirList(HTML);
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML.c_str(), processor);
        response->addHeader("Server","ESP Async Web Server");
        request->send(response);         
      }
    } else {
      if (request->hasParam("filename", true)) { // Download file
        if (request->hasArg("download")) { // File download
          Serial.println("Download Filename: " + request->arg("filename"));
          AsyncWebServerResponse *response = request->beginResponse(SPIFFS, request->arg("filename"), String(), true);
          response->addHeader("Server", "ESP Async Web Server");
          request->send(response);
          return;
        } else if(request->hasArg("delete")) { // Delete file
          if (SPIFFS.remove(request->getParam("filename", true)->value())) {
            logOutput((String)request->getParam("filename", true)->value().c_str() + " was deleted !");
          } else {
            logOutput("Could not delete file. Try again !");
          }
          request->redirect("/files");
        }
      } else if(request->hasArg("goBack")) { // Go Back Button
        request->redirect("register");
      } else if(request->hasArg("restart_device")) {
        request->send(200,"text/plain", "The device will reboot shortly !");
        ESP.restart();
      }
      String HTML PROGMEM; // HTML code 
      String filename = request->url() + ".html";
      File pageFile = SPIFFS.open(filename, "r");
      if (pageFile) {
        HTML = readString(pageFile);
        pageFile.close();
        HTML = addDirList(HTML);
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML.c_str(), processor);
        response->addHeader("Server","ESP Async Web Server");
        request->send(response);         
      }
    }
  }); // server.on("/files", HTTP_ANY, [](AsyncWebServerRequest *request)

  server.on("/staticIP", HTTP_POST, [](AsyncWebServerRequest * request){
    if(request->hasArg("saveStatic")){
      int params = request->params();
      String values_static_post[params];
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
            logOutput((String)"POST[" + p->name() + "]: " + p->value());
            values_static_post[i] = p->value();            
          } else {
              logOutput((String)"GET[" + p->name() + "]: " + p->value());
            }
      } // for(int i=0;i<params;i++)

        if(values_static_post[0] == "WiFi") {
          if(values_static_post[1] != NULL &&
          values_static_post[1].length() != 0 &&
          values_static_post[2] != NULL &&
          values_static_post[2].length() != 0 &&
          values_static_post[3] != NULL &&
          values_static_post[3].length() != 0 &&
          values_static_post[4] != NULL &&
          values_static_post[4].length() != 0 &&
          values_static_post[5] != NULL &&
          values_static_post[5].length() != 0 &&
          values_static_post[6] != NULL &&
          values_static_post[6].length() != 0) {
              File inputsWrite = SPIFFS.open("/networkRelay.txt", "w");
              if(!inputsWrite) logOutput((String)"ERROR_INSIDE_POST ! Couldn't open file to write Static IP credentials !"); 
              inputsWrite.println(values_static_post[0]);   // Connection Type ? WiFi : Ethernet
              inputsWrite.println(values_static_post[1]);   // SSID
              inputsWrite.println(values_static_post[2]);   // Password
              inputsWrite.println(values_static_post[3]);   // Local IP
              inputsWrite.println(values_static_post[4]);   // Gateway
              inputsWrite.println(values_static_post[5]);   // Subnet
              inputsWrite.println(values_static_post[6]);   // DNS
              inputsWrite.close();
              logOutput("Configuration saved !");
              request->send(200, "text/html", (String)"<div style=\"text-align:center; font-family:arial;\">Congratulation !</br></br>You have successfully changed the networks settings.</br></br>The device will now restart and try to apply the new settings.</br></br>Please wait 10 seconds and then press on the \"Go Home\" button to return to the main page.</br></br>If you can't return to the main page please check the entered values.</br></br><form method=\"post\" action=\"http://" + values_static_post[3] + "\"><input type=\"submit\" value=\"Go Home\"/></form></div>"); 
              shouldReboot = true;
        } else request->redirect("/staticIP");
      } else if (values_static_post[0] == "Ethernet") {
          if(values_static_post[3] != NULL &&
          values_static_post[3].length() != 0 &&
          values_static_post[4] != NULL &&
          values_static_post[4].length() != 0 &&
          values_static_post[5] != NULL &&
          values_static_post[5].length() != 0 &&
          values_static_post[6] != NULL &&
          values_static_post[6].length() != 0) {
              File inputsWrite = SPIFFS.open("/networkRelay.txt", "w");
              if(!inputsWrite) logOutput((String)"ERROR_INSIDE_POST ! Couldn't open file to write Static IP credentials !"); 
              inputsWrite.println(values_static_post[0]);   // Connection Type ? WiFi : Ethernet
              inputsWrite.println(values_static_post[3]);   // Local IP
              inputsWrite.println(values_static_post[4]);   // Gateway
              inputsWrite.println(values_static_post[5]);   // Subnet
              inputsWrite.println(values_static_post[6]);   // DNS
              inputsWrite.close();
              logOutput("Configuration saved !");
              Serial.println("New Static IP Address: " + values_static_post[3]);
              request->send(200, "text/html", (String)"<div style=\"text-align:center; font-family:arial;\">Congratulation !</br></br>You have successfully changed the networks settings.</br></br>The device will now restart and try to apply the new settings.</br></br>Please wait 10 seconds and then press on the \"Go Home\" button to return to the main page.</br></br>If you can't return to the main page please check the entered values.</br></br><form method=\"post\" action=\"http://" + values_static_post[3] + "\"><input type=\"submit\" value=\"Go Home\"/></form></div>"); 
              shouldReboot = true;
        } else request->redirect("/staticIP");
      }// if(values_static_post[0] == "WiFi")
    } else {
      request->redirect("/staticIP");
    } // if(request->hasArg("saveStatic"))

  }); // server.on("/staticLogin", HTTP_POST, [](AsyncWebServerRequest * request)

  server.on("/dhcpIP", HTTP_POST, [](AsyncWebServerRequest * request){
    if(request->hasArg("saveDHCP")){
      int params = request->params();
      String values_dhcp_post[params];
      for(int i=0;i<params;i++){
        AsyncWebParameter* p = request->getParam(i);
        if(p->isPost()){
            logOutput((String)"POST[" + p->name() + "]: " + p->value());            
            values_dhcp_post[i] = p->value();            
          } else {
              logOutput((String)"GET[" + p->name() + "]: " + p->value());
            }
      }

      if(values_dhcp_post[0] == "WiFi") {
        if(values_dhcp_post[1] != NULL && values_dhcp_post[1].length() != 0 &&
          values_dhcp_post[2] != NULL && values_dhcp_post[2].length() != 0) {
          File inputsWrite = SPIFFS.open("/networkRelay.txt", "w");
          if(!inputsWrite) logOutput((String)"ERROR_INSIDE_POST ! Couldn't open file to write DHCP IP credentials !");
          inputsWrite.println(values_dhcp_post[0]);  // Connection Type ? WiFi : Ethernet
          inputsWrite.println(values_dhcp_post[1]);  // SSID
          inputsWrite.println(values_dhcp_post[2]);  // Password
          inputsWrite.close();
          logOutput("Configuration saved !");
          request->send(200, "text/html", "<div style=\"text-align:center; font-family:arial;\">Congratulation !</br></br>You have successfully changed the networks settings.</br></br>The device will now restart and try to apply the new settings.</br></br>You can get this device's IP by looking through your AP's DHCP List."); 
          shouldReboot = true;
        } else request->redirect("/dhcpIP");
      } else if (values_dhcp_post[0] == "Ethernet") {
          File inputsWrite = SPIFFS.open("/networkRelay.txt", "w");
          if(!inputsWrite) logOutput((String)"ERROR_INSIDE_POST ! Couldn't open file to write DHCP IP credentials !");
          inputsWrite.println(values_dhcp_post[0]);  // Connection Type ? WiFi : Ethernet
          inputsWrite.close();
          logOutput("Configuration saved !");
          request->send(200, "text/html", "<div style=\"text-align:center; font-family:arial;\">Congratulation !</br></br>You have successfully changed the networks settings.</br></br>The device will now restart and try to apply the new settings.</br></br>You can get this device's IP by looking through your AP's DHCP List."); 
          shouldReboot = true;
      }
    } else {
      request->redirect("/dhcpIP");
    } // if(request->hasArg("saveStatic")) 
  }); // server.on("/dhcpLogin", HTTP_POST, [](AsyncWebServerRequest * request)

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    if(userFlag) {
      if(!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
        return request->requestAuthentication(NULL,false);            
      request->send(SPIFFS, "/update_page.html", "text/html", false, processor);
    } else {
      request->send(SPIFFS, "/update_page.html", "text/html", false, processor);
    }
  });

  server.onFileUpload(handleUpload);
  server.onNotFound([](AsyncWebServerRequest *request){ request->redirect("/home"); });
  server.begin();

  } else {
    networkRead.close();
    isAPmodeOn = true;
    logOutput(WiFi.mode(WIFI_AP) ? "Controller went in AP Mode !" : "Controller couldn't go in AP_MODE. AP_STA_MODE will start.");
    startWiFiAP();
  }
}
  
void loop(){
  if (shouldReboot) {
    logOutput("Restarting in 5 seconds...");
    delay(5000);
    server.reset();
    ESP.restart();
  }
  currentTimeRelayOne = millis();
  currentTimeRelayTwo = millis();

  if(startTimeRelayOne != 0 && !needManualCloseRelayOne) {
    if ((currentTimeRelayOne - startTimeRelayOne) > (Delay1.toInt()*1000)) {
      digitalWrite(RELAY1,LOW);
      status1 = "OFF";
      logOutput((String)relay1 + " closed");
      startTimeRelayOne = 0;
    }
  }

  if(startTimeRelayTwo != 0 && !needManualCloseRelayTwo) {
    if ((currentTimeRelayTwo - startTimeRelayTwo) > (Delay2.toInt()*1000)) {
      digitalWrite(RELAY2,LOW);
      status2 = "OFF";
      logOutput((String)relay2 + " closed");
      startTimeRelayTwo = 0;
    }
  }
  delay(2);

} // void loop() {} 


