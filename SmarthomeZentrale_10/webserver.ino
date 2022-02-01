const char HTML_PAGE[] PROGMEM =
"<!DOCTYPE html>\n"
"<html>\n"
"  <head>\n"
"    <meta http-equiv='content-type' content='text/html; charset=UTF-8'>\n"
"    <title></title>\n"
"    <style type='text/css'>\n"
"body {\n"
"  background-color: silver;\n"
"  text-align: center;\n"
"  font-family: Helvetica,Arial,sans-serif;\n"
"}\n"
"button {\n"
"  border-radius: 5px; \n"
"  border-style: solid; \n"
"  margin: 10px; \n"
"  background-color: #cccccc; \n"
"  color: black; \n"
"  border-color: #666666;\n"
"}\n"
"</style></head>\n"
"  <body>\n"
"    <div style='border-style: solid; width: 300px; border-radius: 10px; background-color: #e1e1e1;'>\n"
"      <h2>Konfigurationsdateien</h2>\n"
"      <form name='upload' method='post' enctype='multipart/form-data'>\n"
"        <input name='file' type='file' accept='application/json'>\n"
"        <div style='height:60px;'> <button name='upwidgets' formaction='/conf/widgets'>\n"
"            Upload Widgets </button> <button name='uprules' formaction='/conf/rules'>\n"
"            Upload Rules </button> </div>\n"
"      </form>\n"
"        <div style='height:60px;'> <a href='/widgets' download='widgets.json'><button>\n"
"            Download Widgets </button></a><a href = '/rules' download='rules.conf'> <button>\n"
"            Download Rules </button></a> </div>\n"
"    </div>\n"
"  </body>\n"
"</html>\n";

void sendRules() {
  File f = SPIFFS.open(AUTO_RULES_FILENAME,"r");
  if (f) {
    if (server.streamFile(f,"application/json") != f.size()) {
      Serial.println("Sent less data than expected!");
    }
  }
}

void sendWidgets() {
  File dir,f;
  uint8_t page = 0;
  uint8_t widget = 0;
  uint8_t cnt = 0;
  String fName, props;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "{\"pages\":[{\"widgets\":["); 
  dir = SPIFFS.open("/wdgconf","r");
  if (dir) {
    f = dir.openNextFile();
    while (f) {
      fName = f.name();
      sscanf(fName.c_str(),"/wdgconf/p %i /w %i",&page,&widget);
      props = f.readString();
      if (page > cnt) {
        server.sendContent("]},{\"widgets\":[");
        cnt++;
      } else {
        if (widget > 0) server.sendContent(",");
      }
      server.sendContent(props);
      f = dir.openNextFile();
    }
    f.close();
    server.sendContent("]}]}");
  }

}

void handleConfig() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", HTML_PAGE); 
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  Serial.println("Upload");
  Serial.println(upload.name);
  Serial.println(server.args());
  if (server.uri() =="/conf/widgets") {
    uploadWidgets();
  }
  if (server.uri() == "/conf/rules") {
    uploadRules();
  }
}

void uploadWidgets() {
  File f,dir;
  char buf[30];
  JsonArray pages;
  JsonArray widgets;
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.print("Upload widgets: START, filename: "); Serial.println(upload.filename);
    bufpos = 0;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if ((bufpos+upload.currentSize) < (MAXBUFFER-1)) {
      memcpy(&buffer[bufpos],&upload.buf,upload.currentSize);
    } else {
      Serial.println("Overflow");
    }
    bufpos += upload.currentSize;
    Serial.printf("%i bytes\n",bufpos);
  } else if (upload.status == UPLOAD_FILE_END) {
    Serial.print("Upload: END, Size: "); Serial.println(upload.totalSize);
    buffer[upload.totalSize] = 0;
    Serial.println(buffer);
    DynamicJsonDocument doc(16000);
    DeserializationError err = deserializeJson(doc,buffer,upload.totalSize);
    if (err) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(err.c_str());
    } else {
      //first remove all old data
      for (int16_t i = ats.getPageCount() - 1; i >=0; i--) ats.removePage(i);
      Serial.println("Delete existing");
      dir = SPIFFS.open("/wdgconf","r");
      if (dir){
        f=dir.openNextFile();
        while (f) {
          SPIFFS.remove(f.name());
          f=dir.openNextFile();
        }
      }
      pages = doc["pages"].as<JsonArray>();
      Serial.printf("%i Pages\n",pages.size());
      for (uint8_t p=0; p < pages.size(); p++) {
        widgets = pages[p]["widgets"].as<JsonArray>();
        Serial.printf("%i Widgets\n",widgets.size());
        for (uint8_t w=0; w < widgets.size(); w++) {
          sprintf(buf,"/wdgconf/p%i/w%i",p,w);
          Serial.printf("Filename %s\n",buf);
          f = SPIFFS.open(buf,"w+");
          serializeJson(widgets[w].as<JsonObject>(),f);
          f.close();
        }
      }
      ats.loadAllPages();
      ats.drawPage();
      //ESP.restart();   
    }
  }
}

void uploadRules() {
  File f;
  uint16_t sz;
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadfile = SPIFFS.open(AUTO_RULES_FILENAME,"w");
    if (uploadfile) Serial.println("File OK");
    Serial.print("Upload rules: START, filename: "); Serial.println(upload.filename);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    sz = upload.currentSize;
    Serial.printf("Write %i bytes\n",sz );
    sz = uploadfile.write(upload.buf, upload.currentSize);
    Serial.printf("%i bytes written \n",sz );
  } else if (upload.status == UPLOAD_FILE_END) {
    Serial.print("Upload: END, Size: "); Serial.println(upload.totalSize);
    uploadfile.close();
    atm.readRules();
  }
}

void initWebServer() {
  server.on("/conf", HTTP_GET, handleConfig);
  server.on("/conf/widgets", HTTP_GET, handleConfig);
  server.on("/conf/rules", HTTP_GET, handleConfig);
  server.on("/widgets", sendWidgets);
  server.on("/rules", sendRules);
  server.on("/conf/widgets", HTTP_POST, handleConfig, handleFileUpload);
  server.on("/conf/rules", HTTP_POST, handleConfig, handleFileUpload);
  server.onNotFound(handleNotFound);

  server.begin();
}
