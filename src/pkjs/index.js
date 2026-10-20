// Phone-side code (runs in the Pebble app): the settings page (Clay) and the
// weather fetch. Weather comes from Open-Meteo using the phone's location and is
// sent to the watch as the "Temp" app message, with today's low and high as "TempMin" and
// "TempMax", all in tenths of a degree Celsius (the watch picks Celsius or Fahrenheit), with a
// picture for the weather ("Condition") and today's and tomorrow's sunrise and sunset (Unix
// times); the watch asks for a refresh with "RequestWeather". Clay delivers the settings itself.

var Clay = require('pebble-clay');
var clayConfig = require('./config');
var customClay = require('./custom-clay');
// The live preview on the settings page runs the face's own code (see tools/preview).
var clay = new Clay(clayConfig, customClay, { userData: { preview: require('./preview-data') } });

var lastFetch = 0;                 // last attempt in this run of the phone-side code
var MIN_FETCH_MS = 5 * 60 * 1000;  // never fetch more often than this (a fix and a web request cost battery)
var MIN_LAUNCH_MS = 25 * 60 * 1000; // opening the face again soon after a fetch needs no new one
var STORE_KEY = 'lcd221-last-weather';

function lastWeatherTime() {
  try { return parseInt(localStorage.getItem(STORE_KEY), 10) || 0; } catch (e) { return 0; }
}

function rememberWeatherTime() {
  try { localStorage.setItem(STORE_KEY, String(Date.now())); } catch (e) { /* storage unavailable */ }
}

// The watch's weather pictures (WX_* in main.c) for Open-Meteo's WMO weather codes.
function condition(code, isDay) {
  if (code === 0) return isDay ? 1 : 2;                         // clear: sun, or moon
  if (code === 1 || code === 2) return isDay ? 3 : 4;           // partly cloudy
  if (code === 3) return 5;                                     // overcast
  if (code === 45 || code === 48) return 9;                     // fog
  if (code >= 95) return 8;                                     // thunderstorm
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return 7;  // snow
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 6;  // drizzle, rain, showers
  return 0;                                                     // unknown: no picture
}

// minAgeMs: skip the fetch if the last successful one is more recent than this.
function fetchWeather(minAgeMs) {
  if (Date.now() - lastFetch < MIN_FETCH_MS) return;
  if (Date.now() - lastWeatherTime() < minAgeMs) return;
  lastFetch = Date.now();
  navigator.geolocation.getCurrentPosition(function (pos) {
    var url = 'https://api.open-meteo.com/v1/forecast' +
      '?latitude=' + pos.coords.latitude.toFixed(3) +
      '&longitude=' + pos.coords.longitude.toFixed(3) +
      '&current=temperature_2m,weather_code,is_day' +
      '&daily=temperature_2m_min,temperature_2m_max,sunrise,sunset' +
      '&hourly=precipitation_probability&forecast_hours=2' +  // this hour and the next
      '&forecast_days=2&timezone=auto' +  // "today" is the local day where the phone is
      '&timeformat=unixtime';
    var xhr = new XMLHttpRequest();
    xhr.onload = function () {
      try {
        var data = JSON.parse(this.responseText), cur = data.current;
        if (!cur || typeof cur.temperature_2m !== 'number') throw new Error('no temperature in reply');
        var msg = {
          Temp: Math.round(cur.temperature_2m * 10)  // tenths of a degree Celsius; the watch converts
        };
        var daily = data.daily;
        var lo = daily && daily.temperature_2m_min && daily.temperature_2m_min[0];
        var hi = daily && daily.temperature_2m_max && daily.temperature_2m_max[0];
        if (typeof lo === 'number' && typeof hi === 'number') {
          msg.TempMin = Math.round(lo * 10);
          msg.TempMax = Math.round(hi * 10);
        }
        // The chance of rain in the next hour: the higher of this hour's and the next one's.
        var rain = data.hourly && data.hourly.precipitation_probability;
        if (rain && rain.length) {
          var chance = Math.max.apply(null, rain.slice(0, 2).filter(function (v) { return typeof v === 'number'; }));
          if (isFinite(chance)) msg.Rain = Math.round(chance);
        }
        if (typeof cur.weather_code === 'number') msg.Condition = condition(cur.weather_code, cur.is_day === 1);
        // (Near the poles a day may have no sunrise or sunset: that one is left out.)
        ['Sunrise', 'Sunset'].forEach(function (key) {
          var times = daily && daily[key.toLowerCase()];
          if (!times) return;
          if (typeof times[0] === 'number') msg[key] = times[0];
          if (typeof times[1] === 'number') msg[key + '2'] = times[1];
        });
        Pebble.sendAppMessage(msg);
        rememberWeatherTime();
      } catch (e) {
        lastFetch = 0;
        console.log('Weather parse failed: ' + e);
      }
    };
    xhr.onerror = function () { lastFetch = 0; };
    xhr.open('GET', url);
    xhr.send();
  }, function (err) {
    lastFetch = 0;  // a failed fix may be retried on the next request
    console.log('Location failed: ' + err.message);
  }, { timeout: 15000, maximumAge: 30 * 60 * 1000 });
}

// The phone's battery level for the top bezel's middle, now and whenever it changes, where the
// app's JavaScript can read it (the Battery Status API; not on iPhones).
function watchPhoneBattery() {
  if (!navigator.getBattery) return;
  navigator.getBattery().then(function (battery) {
    var last = -1;
    function send(retries) {
      var level = Math.round(battery.level * 100);
      if (level === last && retries === undefined) return;
      last = level;
      Pebble.sendAppMessage({ PhoneBattery: level }, null, function () {
        if ((retries || 0) < 2) setTimeout(function () { send((retries || 0) + 1); }, 3000);
      });
    }
    send();
    battery.addEventListener('levelchange', function () { send(); });
  }).catch(function () { /* not available */ });
}

// The watch face is opened often (after the menu, a notification...): only fetch if the last
// reading is old. The watch keeps its own copy and asks again when it needs fresh data.
Pebble.addEventListener('ready', function () {
  fetchWeather(MIN_LAUNCH_MS);
  watchPhoneBattery();
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload.RequestWeather) fetchWeather(0);
});
