///////////////////////////////////////////// WIFI
// ===== WiFi AP File Server (single definition) =====

// ---- HTML ----
const char* headerHtml = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 SD Manager</title>
  <style>
    body { font-family: Arial, sans-serif; max-width: 800px; margin: auto; padding: 10px; }
    table { width: 100%; border-collapse: collapse; margin-top: 10px; }
    th, td { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }
    th { background-color: #f4f4f4; }
    button { margin: 2px; padding: 5px 10px; }
    input[type=file] { margin-top: 10px; }
    progress { width: 100%; margin-top: 5px; }
    #uploadStatus { display: inline-block; margin-left: 10px; }
    .toolbar { margin-top:10px; padding:10px; border:1px solid #ddd; background:#fafafa; }
    .error { color: red; font-weight: bold; }
    .loading { color: #666; font-style: italic; }
  </style>
  <script>
    function downloadFile(fname) {
      var xhr = new XMLHttpRequest();
      xhr.open('GET', '/download?file=' + encodeURIComponent(fname), true);
      xhr.responseType = 'blob';
      xhr.onprogress = function(event) {
        var percent = event.lengthComputable ? Math.floor((event.loaded / event.total) * 100) : '';
        var el = document.getElementById('progress_' + fname);
        if (el) el.innerText = percent ? percent + '%' : '';
      };
      xhr.onload = function() {
        var url = window.URL.createObjectURL(xhr.response);
        var a = document.createElement('a');
        a.href = url;
        a.download = fname;
        a.click();
        window.URL.revokeObjectURL(url);
      };
      xhr.send();
    }
    function deleteFile(fname) {
      if (confirm('Delete ' + fname + '?')) {
        var xhr = new XMLHttpRequest();
        xhr.open('GET', '/delete?file=' + encodeURIComponent(fname), true);
        xhr.onload = function() { location.reload(); };
        xhr.send();
      }
    }
    function renameFile(fname) {
      var newname = prompt('Rename ' + fname + ' to:', fname);
      if (newname && newname !== fname) {
        var xhr = new XMLHttpRequest();
        xhr.open('GET', '/rename?file=' + encodeURIComponent(fname) + '&newname=' + encodeURIComponent(newname), true);
        xhr.onload = function() { location.reload(); };
        xhr.send();
      }
    }
    function uploadFile() {
      var fileInput = document.getElementById('fileInput');
      if (fileInput.files.length == 0) { alert('Select a file'); return; }
      var file = fileInput.files[0];
      var formData = new FormData();
      formData.append('upload', file);

      var xhr = new XMLHttpRequest();
      xhr.open('POST', '/upload', true);

      xhr.upload.onprogress = function(event) {
        var percent = event.lengthComputable ? Math.floor((event.loaded / event.total) * 100) : 0;
        document.getElementById('uploadProgress').value = percent;
        document.getElementById('uploadStatus').innerText = percent + '%';
      };
      xhr.onload = function() {
        document.getElementById('uploadStatus').innerText = (xhr.status === 200) ? 'Upload complete' : 'Upload failed';
        setTimeout(function() { location.reload(); }, 1000);
      };
      xhr.send(formData);
    }
    function deleteAll(){
      if (confirm('Delete ALL files from SD?')) {
        var xhr = new XMLHttpRequest();
        xhr.open('POST','/delete_all',true);
        xhr.onload = function(){ alert(xhr.responseText); location.reload(); };
        xhr.send();
      }
    }

    // Fetch con timeout y reintentos
    function fetchWithRetry(url, elementId, retries = 3, timeout = 5000) {
      var attempt = 0;
      function tryFetch() {
        var xhr = new XMLHttpRequest();
        var timeoutId;

        xhr.open('GET', url, true);
        xhr.timeout = timeout;

        xhr.onload = function() {
          clearTimeout(timeoutId);
          if (xhr.status === 200) {
            document.getElementById(elementId).innerText = xhr.responseText;
            document.getElementById(elementId).className = '';
          } else {
            retry();
          }
        };

        xhr.onerror = function() {
          clearTimeout(timeoutId);
          retry();
        };

        xhr.ontimeout = function() {
          clearTimeout(timeoutId);
          retry();
        };

        function retry() {
          attempt++;
          if (attempt < retries) {
            document.getElementById(elementId).innerText = 'Retry ' + attempt + '/' + retries + '...';
            setTimeout(tryFetch, 500);
          } else {
            document.getElementById(elementId).innerText = 'Failed';
            document.getElementById(elementId).className = 'error';
          }
        }

        xhr.send();
      }
      tryFetch();
    }
  </script>
</head>
<body>
  <h1>HIRI-PRO SD File Manager</h1>

  <div class="toolbar">
    <div><b>AP SSID:</b> <span id="ssid" class="loading">Loading...</span> &nbsp; <b>Password:</b> 12345678 &nbsp; <b>IP:</b> <span id="ip" class="loading">Loading...</span></div>
    <div style="margin-top:8px">
      <input type="file" id="fileInput">
      <button onclick="uploadFile()">Upload</button>
      <button onclick="deleteAll()">Delete ALL files</button>
      <progress id="uploadProgress" value="0" max="100"></progress>
      <span id="uploadStatus"></span>
    </div>
  </div>

  <table>
    <tr><th>Name</th><th>Size (bytes)</th><th>Actions</th><th>Progress</th></tr>
)rawliteral";

const char* footerHtml = R"rawliteral(
  </table>
  <script>
    // Cargar IP y SSID con reintentos automáticos
    window.onload = function() {
      fetchWithRetry('/ip', 'ip', 5, 3000);
      fetchWithRetry('/ssid', 'ssid', 5, 3000);
    };
  </script>
</body>
</html>
)rawliteral";

#include <ctype.h>

// Replace non-alphanumeric chars by '_'
static String sanitizeForId(const String& s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    out += (isalnum((unsigned char)c) ? c : '_');
  }
  return out;
}

// ---- Routes implementation ----
void listFiles() {
  // Enviar header primero
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(headerHtml);

  // Iterar archivos y enviar cada fila
  File root = SD.open("/");
  if (root) {
    File file = root.openNextFile();
    int fileCount = 0;
    while (file && fileCount < 100) {  // Límite de seguridad
      if (!file.isDirectory()) {
        String name = file.name();
        size_t fsize = file.size();
        file.close();

        String row = "<tr>";
        row += "<td>" + name + "</td>";
        row += "<td>" + String(fsize) + "</td>";
        row += "<td>";
        row += "<button onclick=\"downloadFile('" + name + "')\">Download</button>";
        row += "<button onclick=\"deleteFile('" + name + "')\">Delete</button>";
        row += "<button onclick=\"renameFile('" + name + "')\">Rename</button>";
        row += "</td>";
        row += "<td><span id='progress_" + name + "'></span></td>";
        row += "</tr>\n";

        server.sendContent(row);
        fileCount++;
      } else {
        file.close();
      }
      file = root.openNextFile();
      yield();  // Dar tiempo al watchdog
    }
    root.close();
  }

  // Enviar footer
  server.sendContent(footerHtml);
  server.sendContent("");  // Finalizar chunked encoding
}

void handleFileDownload() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "File not specified");
    return;
  }
  String filename = server.arg("file");
  File f = SD.open("/" + filename);
  if (!f) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server.sendHeader("Content-Length", String(f.size()));
  server.streamFile(f, "application/octet-stream");
  f.close();
}

void handleFileDelete() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "File not specified");
    return;
  }
  String filename = server.arg("file");
  bool ok = SD.remove("/" + filename);
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Delete failed");
}

void handleFileRename() {
  if (!server.hasArg("file") || !server.hasArg("newname")) {
    server.send(400, "text/plain", "Missing parameters");
    return;
  }
  String oldname = server.arg("file");
  String newname = server.arg("newname");
  bool ok = SD.rename("/" + oldname, "/" + newname);
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "Rename failed");
}

void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    uploadFile = SD.open(filename, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
  }
}

void handleDeleteAll() {
  File root = SD.open("/");
  if (!root) {
    server.send(500, "text/plain", "SD open error");
    return;
  }
  int okCnt = 0, failCnt = 0;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String name = file.name();
      file.close();  // cerrar antes de borrar
      if (SD.remove("/" + name)) okCnt++;
      else failCnt++;
      root.close();  // reiniciar iteración limpia
      root = SD.open("/");
      file = root.openNextFile();
      continue;
    }
    file = root.openNextFile();
  }
  root.close();
  server.send(200, "text/plain", "Deleted: " + String(okCnt) + ", Failed: " + String(failCnt));
}

void handleIp() {
  server.send(200, "text/plain", WiFi.softAPIP().toString());
}

void handleSsid() {
  server.send(200, "text/plain", AP_SSID_STR);
}

void setupWifiRoutes() {
  server.on("/", HTTP_GET, listFiles);
  server.on("/download", HTTP_GET, handleFileDownload);
  server.on("/delete", HTTP_GET, handleFileDelete);
  server.on("/rename", HTTP_GET, handleFileRename);
  server.on(
    "/upload", HTTP_POST, []() {
      server.send(200, "text/plain", "OK");
    },
    handleFileUpload);
  server.on("/delete_all", HTTP_POST, handleDeleteAll);
  server.on("/ip", HTTP_GET, handleIp);
  server.on("/ssid", HTTP_GET, handleSsid);
}

//-----------handrles start/stop wifi server
void startWifiApServer() {
  // Monta SD si no está
  if (!SDOK) {
    spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    SDOK = SD.begin(SD_CS, spiSD);
    Serial.println(SDOK ? "[SD] Ready" : "[SD] FAIL");
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID_STR.c_str(), AP_PASSWORD);
  delay(100);

  setupWifiRoutes();
  server.begin();
  wifiModeActive = true;

  IPAddress ip = WiFi.softAPIP();
  apIpStr = ip.toString();  // actualizar string mostrado en OLED
  Serial.print("[WiFi] AP SSID: ");
  Serial.println(AP_SSID_STR);
  Serial.print("[WiFi] AP IP: ");
  Serial.println(ip);

  // OLED banner de Wi-Fi
  oledStatus("WIFI MODE",
             "SSID: " + AP_SSID_STR,
             "PASS: " + String(AP_PASSWORD),
             "IP: " + ip.toString());
}

void stopWifiApServer() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiModeActive = false;
  Serial.println("[WiFi] AP stopped");
  // (opcional) refrescar OLED con tu UI normal al siguiente ciclo
}
