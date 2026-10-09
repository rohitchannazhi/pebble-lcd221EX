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
          { "label": "Silver", "value": "silver" }
        ]
      },
      {
        "type": "toggle", "messageKey": "Inverted", "label": "Inverted colors",
        "defaultValue": false,
        "description": "Light digits on a dark LCD, like a negative-display watch. The case keeps its color."
      },
      {
        "type": "select", "messageKey": "DigitStyle", "label": "Digit style", "defaultValue": "segment",
        "description": "7-segment is the LCD look, with faint unlit segments. The fonts are used for every text and number on the LCD (not the top and bottom bars).",
        "options": [
          { "label": "7-segment", "value": "segment" },
          { "label": "Condensed (based on Saira)", "value": "saira" },
          { "label": "Handjet", "value": "handjet" },
          { "label": "Angular (based on Iceberg)", "value": "iceberg" },
          { "label": "Big Shoulders Stencil", "value": "stencil" }
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
        "description": "How BT, CHG, DST and MUTE are shown next to the weekday.",
        "options": [
          { "label": "Pills", "value": "pills" },
          { "label": "Active only", "value": "active" },
          { "label": "Icons", "value": "icons" }
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
      colorItem("ColMute", "MUTE", "000000"),
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
