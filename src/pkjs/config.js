// Vibration patterns; the values are the watch's VibeChoice enum (see main.c).
var VIBE_PATTERNS = [
  { "label": "None", "value": "0" },
  { "label": "Short pulse", "value": "1" },
  { "label": "Long pulse", "value": "2" },
  { "label": "Double pulse", "value": "3" },
  { "label": "Triple pulse", "value": "4" },
  { "label": "Heartbeat", "value": "5" },
  { "label": "SOS", "value": "6" }
];

// The "Custom colors" section: one colour picker per part of the face, all in the group
// "colors" so custom-clay.js can show them only while "Use custom colors" is on. The defaults
// are the normal theme (black on white); the messageKeys are read in main.c (COLOR_KEYS).
function colorHeading(title) {
  return { "type": "heading", "defaultValue": title, "size": 4, "group": "colors" };
}
function colorItem(key, label, defaultValue) {
  return { "type": "color", "messageKey": key, "label": label, "defaultValue": defaultValue, "group": "colors" };
}

module.exports = [
  { "type": "heading", "defaultValue": "LCD 221" },
  {
    "type": "section",
    "items": [
      {
        "type": "button", "id": "resetDefaults", "defaultValue": "Reset to defaults",
        "description": "Tap twice to put every setting back to its default, then tap Save at the bottom to apply."
      }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Time & date" },
      {
        "type": "select", "messageKey": "TimeFormat", "label": "Time format", "defaultValue": "auto",
        "description": "Follow watch uses the 12/24-hour setting of your watch.",
        "options": [
          { "label": "Follow watch", "value": "auto" },
          { "label": "24-hour", "value": "24" },
          { "label": "12-hour", "value": "12" }
        ]
      },
      {
        "type": "toggle", "messageKey": "TimeZero", "label": "Leading zero in the hour", "defaultValue": true,
        "description": "24-hour format only. Off shows 7:05 instead of 07:05, like the original watch."
      },
      {
        "type": "toggle", "messageKey": "TimeZero12", "label": "Leading zero in 12-hour time", "defaultValue": false,
        "description": "Shows 07:05 instead of 7:05. The P beside the hours makes way: PM is shown with the indicators instead, in place of CHG (the battery icon shows a bolt while charging)."
      },
      {
        "type": "select", "messageKey": "RangeMarks", "label": "Min / max marks", "defaultValue": "tall",
        "description": "The bottom row shows today's low and high temperature, for where your phone is.",
        "options": [
          { "label": "Tall arrows", "value": "tall" },
          { "label": "Triangles", "value": "triangles" },
          { "label": "LO / HI", "value": "lohi" }
        ]
      }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Temperature" },
      {
        "type": "select", "messageKey": "TempUnit", "label": "Temperature unit",
        "defaultValue": "auto",
        "description": "Follow watch shows Fahrenheit when your watch uses imperial units, otherwise Celsius.",
        "options": [
          { "label": "Follow watch", "value": "auto" },
          { "label": "Celsius", "value": "C" },
          { "label": "Fahrenheit", "value": "F" }
        ]
      }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Top bezel" },
      {
        "type": "toggle", "messageKey": "ShowBattery", "label": "Show battery level", "defaultValue": true
      },
      {
        "type": "input", "messageKey": "TopLeftText", "label": "Left text",
        "defaultValue": "30 DAY BATT",
        "description": "Shown instead of the battery level, in capitals. Leave empty for none.",
        "attributes": { "maxlength": 19, "autocapitalize": "characters" }
      },
      {
        "type": "toggle", "messageKey": "ShowSteps", "label": "Show step count", "defaultValue": true
      },
      {
        "type": "input", "messageKey": "TopRightText", "label": "Right text",
        "defaultValue": "WR 3ATM",
        "description": "Shown instead of the step count, in capitals. Leave empty for none.",
        "attributes": { "maxlength": 19, "autocapitalize": "characters" }
      },
      {
        "type": "toggle", "messageKey": "ShowPhoneBattery", "label": "Phone battery",
        "defaultValue": true,
        "description": "Your phone's battery level in the middle, small (while the phone is connected; left out when the texts leave no room). Needs a phone whose Pebble app can read its battery level (Android)."
      }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Bottom bezel" },
      {
        "type": "input", "messageKey": "BezelLabel", "label": "Label", "defaultValue": "PEBBLE",
        "description": "Text on the right of the bottom bezel, shown in capitals. Leave empty for none.",
        "attributes": { "maxlength": 12, "autocapitalize": "characters" }
      },
      {
        "type": "toggle", "messageKey": "ShowSun", "label": "Next sunrise or sunset",
        "defaultValue": true,
        "description": "Between the heart rate and the label, with a sun rising or setting (left out when the label leaves no room). From the weather service, for your location."
      }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Appearance" },
      {
        "type": "select", "messageKey": "CaseColor", "label": "Case color", "defaultValue": "black",
        "options": [
          { "label": "Black", "value": "black" },
          { "label": "Silver", "value": "silver" },
          { "label": "Custom color...", "value": "custom" }
        ]
      },
      {
        "type": "color", "messageKey": "CaseCustom", "label": "Custom case color", "defaultValue": "0055aa",
        "description": "The top and bottom bar text turns white or black to stay readable (unless Use custom colors is on)."
      },
      {
        "type": "toggle", "messageKey": "Inverted", "label": "Inverted colors",
        "defaultValue": false,
        "description": "Light digits on a dark LCD, like a negative-display watch. The case keeps its color."
      },
      {
        "type": "toggle", "messageKey": "NightMode", "label": "Night mode",
        "defaultValue": false,
        "description": "From sunset to sunrise (the times come with the weather): a white case and LCD, black text (no unlit segments), a red heart, dark gray lines, a light blue BT and an amber DST, in the Night digit style below. Custom colors and Inverted colors apply in the day."
      },
      {
        "type": "select", "messageKey": "NightDigits", "label": "Night digit style", "defaultValue": "day",
        "description": "The digits from sunset to sunrise; the day's style comes back at sunrise. Same as day uses 7-segment instead of the dotted styles, too faint at night.",
        "options": [
          { "label": "Same as day", "value": "day" },
          { "label": "7-segment", "value": "segment" },
          { "label": "Classic Bold", "value": "classicbold" },
          { "label": "Hollow segments", "value": "hollow" },
          { "label": "Modern Light", "value": "light" },
          { "label": "Modern Bold", "value": "bold" },
          { "label": "Angular (based on Iceberg)", "value": "iceberg" },
          { "label": "Bebas Neue", "value": "bebas" },
          { "label": "Teko", "value": "teko" },
          { "label": "Tactical (based on Quantico)", "value": "quantico" },
          { "label": "Orbital (based on Orbitron)", "value": "orbitron" },
          { "label": "Squared (based on Squada One)", "value": "squada" }
        ]
      },
      {
        "type": "select", "messageKey": "DigitStyle", "label": "Digit style", "defaultValue": "segment",
        "description": "Every number on the LCD (not the top and bottom bars), with faint unlit segments or dots. Hollow segments and the Modern styles show the weekday in 14-segment letters; the Dot matrix styles and Dotted segments in dots.",
        "options": [
          { "label": "7-segment", "value": "segment" },
          { "label": "Classic Bold", "value": "classicbold" },
          { "label": "Dotted segments", "value": "dotted" },
          { "label": "Hollow segments", "value": "hollow" },
          { "label": "Modern Light", "value": "light" },
          { "label": "Modern Bold", "value": "bold" },
          { "label": "Dot matrix", "value": "matrix" },
          { "label": "Dot matrix, dotted clock", "value": "matrixdot" },
          { "label": "Angular (based on Iceberg)", "value": "iceberg" },
          { "label": "Bebas Neue", "value": "bebas" },
          { "label": "Teko", "value": "teko" },
          { "label": "Tactical (based on Quantico)", "value": "quantico" },
          { "label": "Orbital (based on Orbitron)", "value": "orbitron" },
          { "label": "Squared (based on Squada One)", "value": "squada" }
        ]
      },
      {
        "type": "select", "messageKey": "LineStyle", "label": "Divider lines", "defaultValue": "solid",
        "options": [
          { "label": "Solid", "value": "solid" },
          { "label": "Segmented", "value": "segmented" },
          { "label": "Ruler ticks", "value": "ruler" },
          { "label": "Corner brackets", "value": "brackets" },
          { "label": "HUD chamfer", "value": "hud" }
        ]
      },
      {
        "type": "select", "messageKey": "EdgeStyle", "label": "Window edge", "defaultValue": "match",
        "description": "The lines above and below the display. With Use custom colors on, they take the LCD window edge colour.",
        "options": [
          { "label": "Match the dividers", "value": "match" },
          { "label": "Center tab", "value": "tab" },
          { "label": "Notched", "value": "notched" }
        ]
      },
      {
        "type": "select", "messageKey": "IndicatorStyle", "label": "Indicators", "defaultValue": "pills",
        "description": "How BT, CHG, DST and the chance of rain are shown next to the weekday. During Quiet Time the colon of the time is a line.",
        "options": [
          { "label": "Pills", "value": "pills" },
          { "label": "Active only", "value": "active" },
          { "label": "Icons", "value": "icons" }
        ]
      },
      {
        "type": "toggle", "messageKey": "ShowRain", "label": "Chance of rain",
        "defaultValue": true,
        "description": "The chance of rain in the next hour (e.g. 40%) as an indicator, lit from 50%. From the weather, for your location (the Icons style shows a raindrop)."
      },
      {
        "type": "toggle", "messageKey": "WeatherBox", "label": "Weather layout",
        "defaultValue": false,
        "description": "The weather's picture and the temperature next to the weekday, the indicators in the bottom row's right box (two to a row), and in 12-hour time the P inside the hour's first digit."
      },
      {
        "type": "select", "messageKey": "LcdBackground", "label": "Screen background", "defaultValue": "plain",
        "description": "Dots on a quarter of the background, in the unlit segments' color (light gray, or dark gray with Inverted colors).",
        "options": [
          { "label": "Plain", "value": "plain" },
          { "label": "25% dots", "value": "dots" }
        ]
      },
      {
        "type": "toggle", "messageKey": "Ghosts", "label": "Show unlit segments",
        "defaultValue": true,
        "description": "Faintly show the segments that are off, like a real LCD."
      },
      {
        "type": "select", "messageKey": "BacklightColor", "label": "Backlight color",
        "defaultValue": "system",
        "options": [
          { "label": "System default", "value": "system" },
          { "label": "Amber", "value": "FFA020" },
          { "label": "Warm white", "value": "FFD8A0" },
          { "label": "Red", "value": "FF2000" },
          { "label": "Orange", "value": "FF6000" },
          { "label": "Yellow", "value": "FFE000" },
          { "label": "Green", "value": "30FF40" },
          { "label": "Cyan", "value": "00E0FF" },
          { "label": "Blue", "value": "2060FF" },
          { "label": "Purple", "value": "A040FF" },
          { "label": "Pink", "value": "FF40A0" },
          { "label": "Custom color...", "value": "custom" }
        ]
      },
      {
        "type": "color", "messageKey": "BacklightCustom", "label": "Custom color", "defaultValue": "ffaa00",
        "description": "The backlight LED may look a bit different from the swatch."
      }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Custom colors" },
      {
        "type": "toggle", "messageKey": "CustomColors", "label": "Use custom colors", "defaultValue": false,
        "description": "Choose the color of every part of the watch face except the case, which keeps its Case color. While on, Inverted colors is ignored. The watch has 64 colors, so a color is rounded to the nearest one. Unlit segments are drawn as faint dots, so they look paler than the color you pick."
      },
      colorHeading("Bezels"),
      colorItem("ColTopLeft", "Top bezel, left text", "ffffff"),
      colorItem("ColTopRight", "Top bezel, right text", "ffffff"),
      colorItem("ColBadge", "Bottom bezel, heart", "ffffff"),
      colorItem("ColHeart", "Bottom bezel, heart rate", "ffffff"),
      colorItem("ColLabel", "Bottom bezel, label", "ffffff"),
      colorHeading("LCD panel"),
      colorItem("ColEdge", "LCD window edge", "000000"),
      colorItem("ColLcd", "LCD background", "ffffff"),
      colorItem("ColUnlit", "Unlit segments and labels", "aaaaaa"),
      colorHeading("Weekday and indicators"),
      colorItem("ColWeekday", "Weekday and day", "000000"),
      colorItem("ColBt", "BT", "000000"),
      colorItem("ColChg", "CHG / FULL", "000000"),
      colorItem("ColDst", "DST", "000000"),
      colorItem("ColMute", "Chance of rain", "000000"),
      colorHeading("Time"),
      colorItem("ColHours", "Hour digits", "000000"),
      colorItem("ColColon", "Colon", "000000"),
      colorItem("ColMinutes", "Minute digits", "000000"),
      colorItem("ColPm", "PM marker", "000000"),
      colorHeading("Bottom row"),
      colorItem("ColDate", "Low / high temperature", "000000"),
      colorItem("ColRules", "Divider lines", "000000"),
      colorItem("ColRight", "Temperature", "000000")
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Alerts" },
      {
        "type": "select", "messageKey": "VibeDisconnect", "label": "Vibrate on phone disconnect",
        "defaultValue": "3",
        "options": VIBE_PATTERNS
      },
      {
        "type": "select", "messageKey": "VibeConnect", "label": "Vibrate on phone reconnect",
        "defaultValue": "1",
        "description": "No vibration during Quiet Time.",
        "options": VIBE_PATTERNS
      }
    ]
  },
  { "type": "submit", "defaultValue": "Save" }
];
