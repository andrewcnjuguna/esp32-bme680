let globalTemperature = 0;
let globalHumidity = 0;
let globalPressure = 0;
let globalIAQ = 0;
let globalCarbon = 0;
let globalVOC = 0;
let globalIAQsts = "Unknown";

const express = require('express');
const app = express();

app.use(express.json()); // Built-in middleware to parse JSON
app.use(express.urlencoded({ extended: true })); // Built-in middleware to parse URL-encoded forms

// Endpoint to receive sensor data from ESP32
app.post('/sensor-data', (req, res) => {
  const { temperature, humidity, pressure, IAQ, carbon, VOC, IAQsts } = req.body;
  
  // Update global variables
  globalTemperature = temperature;
  globalHumidity = humidity;
  globalPressure = pressure;
  globalIAQ = IAQ;
  globalCarbon = carbon;
  globalVOC = VOC;
  globalIAQsts = IAQsts;

  console.log("Received data:", req.body);
  res.status(200).send('Data received');
});

// Serve the HTML page
app.get('/', (req, res) => {
  res.send(SendHTML());
});

// Endpoint to provide sensor data as JSON
app.get('/data', (req, res) => {
  res.json({
    temperature: globalTemperature,
    humidity: globalHumidity,
    pressure: globalPressure,
    IAQ: globalIAQ,
    carbon: globalCarbon,
    VOC: globalVOC,
    IAQsts: globalIAQsts
  });
});
function SendHTML() {
	let html = "<!DOCTYPE html><html><head><title>BME680 Webserver</title>";
	html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
	html += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.7.2/css/all.min.css'>";
	html += "<link rel='stylesheet' type='text/css' href='styles.css'>";
	html += "<style>";
	html += "body { background-color: #fff; font-family: sans-serif; color: #333333; font: 12px Helvetica, sans-serif box-sizing: border-box;}";
	html += "#page { margin: 18px; background-color: #fff;}";
	html += ".container { height: inherit; padding-bottom: 18px;}";
	html += ".header { padding: 18px;}";
	//qhtml += ".header h1 { padding-bottom: 0.3em; color: #f4a201; font-size: 25px; font-weight: bold; font-family: Garmond, 'sans-serif'; text-align: center;}";
	html += ".header h1 { padding-bottom: 0.3em; color: #00ff00; font-size: 25px; font-weight: bold; font-family: Garmond, 'sans-serif'; text-align: center;}";
	html += "h2 { padding-bottom: 0.2em; border-bottom: 1px solid #eee; margin: 2px; text-align: center;}";
	html += ".box-full { padding: 18px; border 1px solid #ddd; border-radius: 1em 1em 1em 1em; box-shadow: 1px 7px 7px 1px rgba(0,0,0,0.4); background: #fff; margin: 18px; width: 300px;}";
	html += "@media (max-width: 494px) { #page { width: inherit; margin: 5px auto; } #content { padding: 1px;} .box-full { margin: 8px 8px 12px 8px; padding: 10px; width: inherit;; float: none; } }";
	html += "@media (min-width: 494px) and (max-width: 980px) { #page { width: 465px; margin 0 auto; } .box-full { width: 380px; } }";
	html += "@media (min-width: 980px) { #page { width: 930px; margin: auto; } }";
	html += ".sensor { margin: 10px 0px; font-size: 2.5rem;}";
	html += ".sensor-labels { font-size: 1rem; vertical-align: middle; padding-bottom: 15px;}";
	html += ".units { font-size: 1.2rem;}";
	html += "hr { height: 1px; color: #eee; background-color: #eee; border: none;}";
	html += "</style>";
    html += "<script>";
    html += "function fetchData() {";
    html += "fetch('/data')";
    html += ".then(response => response.json())";
    html += ".then(data => {";
    html += "document.getElementById('temperature').innerText = data.temperature ;";
    html += "document.getElementById('humidity').innerText = data.humidity ;";
    html += "document.getElementById('pressure').innerText = data.pressure ;";
    html += "document.getElementById('IAQ').innerText = data.IAQ;";
    html += "document.getElementById('carbon').innerText = data.carbon ;";
    html += "document.getElementById('VOC').innerText = data.VOC ;";
    html += "document.getElementById('IAQsts').innerText = data.IAQsts;";
    html += "})";
    html += ".catch(error => console.error('Error fetching data:', error));";
    html += "}";
    html += "setInterval(fetchData, 1000); // Fetch data every second";
    html += "document.addEventListener('DOMContentLoaded', fetchData); // Fetch data on page load";
    html += "</script>";
    html += "</head>";
    html += "<body>";
    html += "<div id='page'>";
    html += "<div class='header'>";
    html += "<h1>Air Quality Monitoring System</h1>";
    html += "</div>";
    html += "<div id='content' align='center'>";
    html += "<div class='box-full' align='left'>";
    html += "<h2>IAQ Status: <span id='IAQsts'>Unknown</span></h2>";
    html += "<div class='sensors-container'>";
    // For Temperature
    html += "<div class='sensors'><p class='sensor'><i class='fas fa-thermometer-half' style='color:#0275d8'></i><span class='sensor-labels'> Temperature </span><span id='temperature'>0</span><span class='units'>°C</span></p><hr></div>";
    // For Humidity
    html += "<p class='sensor'><i class='fas fa-tint' style='color:#0275d8'></i><span class='sensor-labels'> Humidity </span><span id='humidity'>0</span><span class='units'>%</span></p><hr>";
    // For Pressure
    html += "<p class='sensor'><i class='fas fa-tachometer-alt' style='color:#ff0040'></i><span class='sensor-labels'> Pressure </span><span id='pressure'>0</span><span class='units'>hPa</span></p><hr>";
    // For VOC IAQ
    html += "<div class='sensors'><p class='sensor'><i class='fab fa-cloudversify' style='color:#483d8b'></i><span class='sensor-labels'> IAQ </span><span id='IAQ'>0</span><span class='units'>PPM</span></p><hr></div>";
    // For C02 Equivalent
    html += "<p class='sensor'><i class='fas fa-smog' style='color:#35b22d'></i><span class='sensor-labels'> Co2 Eq. </span><span id='carbon'>0</span><span class='units'>PPM</span></p><hr>";
    // For Breath VOC
    html += "<p class='sensor'><i class='fas fa-wind' style='color:#0275d8'></i><span class='sensor-labels'> Breath VOC </span><span id='VOC'>0</span><span class='units'>PPM</span></p>";
    html += "</div>";
    html += "</div>";
    html += "</div>";
    html += "</div>";
    html += "</body>";
    html += "</html>";
    return html;
}


function sensorHTML(iconClass, label, id, unit) {
	return `<div class='sensors'><p class='sensor'><i class='${iconClass}' style='color:#0275d8'></i><span class='sensor-labels'> ${label} </span><span id='${id}'>0</span><sup class='units'>${unit}</sup></p><hr></div>`;
}

app.listen(3000, () => console.log('Server running on http://localhost:3000'));
