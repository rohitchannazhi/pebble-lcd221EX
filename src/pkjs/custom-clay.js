// Runs inside the Clay settings page (injected via .toString(), so it must be
// self-contained). It hides settings that don't apply to the current choices and
// wires up the "Reset to defaults" button.
module.exports = function () {
  var clayConfig = this;

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function () {
    function item(key) { return clayConfig.getItemByMessageKey(key); }

    // A custom text only matters while its switch is off.
    [['ShowBattery', 'TopLeftText'], ['ShowSteps', 'TopRightText']].forEach(function (pair) {
      var toggle = item(pair[0]), text = item(pair[1]);
      function sync() { if (toggle.get()) text.hide(); else text.show(); }
      toggle.on('change', sync);
      sync();
    });

    // The hour's leading zero only matters in the 24-hour format ("Follow watch" may be either).
    // The 12-hour one only matters in 12-hour time.
    var timeFormat = item('TimeFormat'), hourZero = item('TimeZero'), hourZero12 = item('TimeZero12');
    function syncHourZero() {
      if (timeFormat.get() === '12') hourZero.hide(); else hourZero.show();
      if (timeFormat.get() === '24') hourZero12.hide(); else hourZero12.show();
    }
    timeFormat.on('change', syncHourZero);
    syncHourZero();

    // The seconds mode only matters while the right box shows the seconds; the temperature
    // unit while a temperature is shown: in the right box (which includes the idle time of
    // "after a shake") or the high and low in the date box (the "Min / max" date format).
    var rightBox = item('RightBox'), secondsMode = item('SecondsMode'), unit = item('TempUnit');
    var duration = item('SecondsDuration'), dateFormat = item('DateFormat'), marks = item('RangeMarks');
    function syncRightBox() {
      var seconds = rightBox.get() === 'seconds', minmax = dateFormat.get() === 'minmax';
      if (seconds) secondsMode.show(); else secondsMode.hide();
      if (seconds && secondsMode.get() === 'shake') duration.show(); else duration.hide();
      if (!seconds || secondsMode.get() === 'shake' || minmax) unit.show(); else unit.hide();
      if (minmax) marks.show(); else marks.hide();
    }
    rightBox.on('change', syncRightBox);
    secondsMode.on('change', syncRightBox);
    dateFormat.on('change', syncRightBox);
    syncRightBox();

    // Slanted digits are a 7-segment thing; the fonts are upright.
    var digitStyle = item('DigitStyle'), slanted = item('Slanted');
    function syncDigits() { if (digitStyle.get() === 'segment') slanted.show(); else slanted.hide(); }
    digitStyle.on('change', syncDigits);
    syncDigits();

    // The colour picker only matters while "Custom color..." is the backlight choice.
    var backlight = item('BacklightColor'), custom = item('BacklightCustom');
    function syncCustom() { if (backlight.get() === 'custom') custom.show(); else custom.hide(); }
    backlight.on('change', syncCustom);
    syncCustom();

    // The individual colours only matter while "Use custom colors" is on (they are all in the
    // group "colors"); the Inverted switch is replaced by them then. (Case color always applies.)
    var customColors = item('CustomColors'), inverted = item('Inverted');
    function syncColors() {
      var on = !!customColors.get();
      clayConfig.getItemsByGroup('colors').forEach(function (it) { if (on) it.show(); else it.hide(); });
      if (on) inverted.hide(); else inverted.show();
    }
    customColors.on('change', syncColors);
    syncColors();

    // Live preview: the face as it would look with the settings as they are now, before Save.
    // The watch face's own drawing code runs here, built as WebAssembly (tools/preview/build.sh,
    // handed over by index.js). It gets the settings the way the watch does on Save and draws a
    // frame; the page shows it, redrawn on every change and every second. Only the system-font
    // text (top bar, bottom bezel, 7-segment indicator labels) is the phone's bold font, close to
    // the watch's. The battery, steps, heart rate and weather are sample values.
    (function () {
      var data = clayConfig.meta.userData && clayConfig.meta.userData.preview;
      if (!data || typeof WebAssembly === 'undefined' || !window.atob) return;
      var W = 200, H = 228;
      function bytes(b64) {
        var bin = atob(b64), out = new Uint8Array(bin.length);
        for (var i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
        return out;
      }
      function utf8(s) { return unescape(encodeURIComponent(s)); }  // one char per byte
      var fonts = data.fonts.map(bytes);
      var api = null;
      function heap() { return new Uint8Array(api.memory.buffer); }
      function cstring(p) {
        var u = heap(), s = '';
        while (u[p]) s += String.fromCharCode(u[p++]);
        try { return decodeURIComponent(escape(s)); } catch (e) { return s; }
      }

      // The page: a box fixed at the top, above the settings (a spacer keeps its room at the top
      // of the page). Tap to shrink it or grow it back.
      var box = document.createElement('div'), spacer = document.createElement('div');
      box.style.cssText = 'position:fixed;top:0;left:0;right:0;z-index:10;background:#333;' +
        'text-align:center;padding:.5rem 0 .4rem;border-bottom:1px solid #555';
      var canvas = document.createElement('canvas');
      canvas.width = W;
      canvas.height = H;
      canvas.style.cssText = 'width:200px;height:228px;image-rendering:pixelated;' +
        'image-rendering:crisp-edges;display:block;margin:0 auto;border-radius:6px';
      var note = document.createElement('div');
      note.style.cssText = 'font-size:.7rem;color:#aaa;line-height:1.3;margin-top:.3rem';
      note.textContent = 'Preview of the unsaved settings (sample readings) \u00b7 tap to shrink';
      box.appendChild(canvas);
      box.appendChild(note);
      document.body.insertBefore(box, document.body.firstChild);
      document.body.insertBefore(spacer, box.nextSibling);
      function fitSpacer() { spacer.style.height = box.offsetHeight + 'px'; }
      fitSpacer();
      var small = false;
      box.addEventListener('click', function () {
        small = !small;
        canvas.style.width = small ? '100px' : '200px';
        canvas.style.height = small ? '114px' : '228px';
        note.textContent = small ? 'Preview \u00b7 tap to enlarge'
          : 'Preview of the unsaved settings (sample readings) \u00b7 tap to shrink';
        fitSpacer();
      });
      var screen = canvas.getContext('2d'), image = screen.createImageData(W, H);

      // The system font: Gothic 18 bold (capitals 11px tall, 7px below the box top) and
      // Gothic 24 bold (14px, 10px below), drawn into the face's framebuffer pixel by pixel.
      var scratch = document.createElement('canvas');
      scratch.width = W;
      scratch.height = 40;
      var pen = scratch.getContext('2d', { willReadFrequently: true });
      // Gothic is narrower than the phone's sans-serif: the text is squeezed to about its width.
      var FAMILY = 'bold %spx Roboto, "Helvetica Neue", Arial, sans-serif', SQUEEZE = 0.8;
      var sizes = [0, 0];
      function setFont(big) {
        if (!sizes[big]) {
          var cap = big ? 14 : 11;
          pen.font = FAMILY.replace('%s', 100);
          var m = pen.measureText('H');
          var ascent = m.actualBoundingBoxAscent || 72;
          sizes[big] = Math.round(cap * 100 / ascent * 10) / 10;
        }
        pen.font = FAMILY.replace('%s', sizes[big]);
      }
      var env = {
        js_text: function (p, big, x, y, w, h, align, argb) {
          var text = cstring(p);
          if (w <= 0 || h <= 0) return;
          setFont(big);
          var top = big ? 10 : 7, cap = big ? 14 : 11;
          var tw = pen.measureText(text).width * SQUEEZE;
          var tx = align === 1 ? (w - tw) / 2 : align === 2 ? w - tw : 0;
          pen.clearRect(0, 0, W, 40);
          pen.fillStyle = '#fff';
          pen.textBaseline = 'alphabetic';
          pen.setTransform(SQUEEZE, 0, 0, 1, Math.round(tx), 0);
          pen.fillText(text, 0, top + cap);
          pen.setTransform(1, 0, 0, 1, 0, 0);
          var px = pen.getImageData(0, 0, Math.min(w, W), Math.min(h, 40)).data, fb = heap(), base = api.preview_fb();
          for (var j = 0; j < Math.min(h, 40); j++) {
            for (var i = 0; i < Math.min(w, W); i++) {
              var fx = x + i, fy = y + j;
              if (fx < 0 || fx >= W || fy < 0 || fy >= H) continue;
              if (px[(j * Math.min(w, W) + i) * 4 + 3] >= 110) fb[base + fy * W + fx] = argb;
            }
          }
        },
        js_text_width: function (p, big) {
          setFont(big);
          return Math.ceil(pen.measureText(cstring(p)).width * SQUEEZE);
        },
        js_resource_size: function (id) { return fonts[id - 1] ? fonts[id - 1].length : 0; },
        js_resource_load: function (id, p, n) { heap().set(fonts[id - 1].subarray(0, n), p); }
      };

      function setTime() {
        var now = new Date(), y = now.getFullYear();
        var yday = Math.round((new Date(y, now.getMonth(), now.getDate()) - new Date(y, 0, 1)) / 864e5);
        var jan = new Date(y, 0, 1).getTimezoneOffset(), jul = new Date(y, 6, 1).getTimezoneOffset();
        var dst = now.getTimezoneOffset() < Math.max(jan, jul) ? 1 : 0;
        // "Follow watch": the phone's own 12/24-hour choice stands in for the watch's.
        var h24 = /[ap]\.?m\.?/i.test(new Date(2000, 0, 1, 13).toLocaleTimeString()) ? 0 : 1;
        api.preview_time(Math.floor(now / 1000), y, now.getMonth(), now.getDate(), now.getDay(), yday,
                         now.getHours(), now.getMinutes(), now.getSeconds(), dst, h24);
      }
      function show() {
        api.preview_draw();
        var fb = heap(), base = api.preview_fb(), px = image.data;
        for (var i = 0; i < W * H; i++) {
          var c = fb[base + i];
          px[i * 4] = ((c >> 4) & 3) * 85;
          px[i * 4 + 1] = ((c >> 2) & 3) * 85;
          px[i * 4 + 2] = (c & 3) * 85;
          px[i * 4 + 3] = 255;
        }
        screen.putImageData(image, 0, 0);
      }
      // Every setting, as Clay sends it on Save: switches as 1 or 0, the rest as they are.
      function update() {
        setTime();
        api.preview_begin();
        clayConfig.getAllItems().forEach(function (it) {
          if (!it.messageKey) return;
          var v = it.get(), str = api.preview_str(), u = heap();
          var name = it.messageKey;
          if (typeof v === 'boolean') v = v ? 1 : 0;
          if (typeof v === 'number') {
            for (var i = 0; i < name.length; i++) u[str + i] = name.charCodeAt(i);
            u[str + name.length] = 0;
            api.preview_add_int(Math.round(v));
          } else if (typeof v === 'string') {
            var s = name + '\0' + utf8(v).substring(0, 400) + '\0';
            for (var k = 0; k < s.length; k++) u[str + k] = s.charCodeAt(k);
            api.preview_add_str();
          }
        });
        api.preview_send();
        show();
      }

      WebAssembly.instantiate(bytes(data.wasm), { env: env }).then(function (result) {
        api = result.instance.exports;
        setTime();
        api.preview_init();
        update();
        clayConfig.getAllItems().forEach(function (it) {
          if (it.messageKey) it.on('change', update);
        });
        // Text boxes: as they are typed in, too.
        Array.prototype.forEach.call(document.querySelectorAll('input[type=text]'), function (el) {
          el.addEventListener('input', update);
        });
        setInterval(function () { setTime(); show(); }, 1000);
      }).catch(function (e) {
        note.textContent = 'Preview unavailable: ' + e;
      });
    })();

    // One line naming the latest commit on GitHub, to compare with the commit the installed build
    // was made from (CloudPebble shows it). The page has no other way to know the build's commit.
    var commitInfo = clayConfig.getItemById('commitInfo');
    try {
      var xhr = new XMLHttpRequest();
      xhr.open('GET', 'https://api.github.com/repos/rohitchannazhi/pebble-lcd221ex/commits/main');
      xhr.onload = function () {
        try {
          var c = JSON.parse(xhr.responseText);
          var when = new Date(c.commit.committer.date);
          var months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
          commitInfo.set('Latest commit: ' + c.sha.substring(0, 7) + ' (' + when.getDate() + ' ' +
                         months[when.getMonth()] + ' ' + when.getFullYear() + ')');
        } catch (e) {
          commitInfo.set('Latest commit: unavailable');
        }
      };
      xhr.onerror = function () { commitInfo.set('Latest commit: unavailable (offline?)'); };
      xhr.send();
    } catch (e) {
      commitInfo.set('Latest commit: unavailable');
    }

    // Reset: every setting goes back to the defaultValue declared in config.js;
    // the user then taps Save. The change events above keep hidden fields in sync.
    // It takes two taps within a few seconds, so a stray tap does nothing. (A confirm()
    // dialog is not used because the Pebble app's page view may not show one.)
    var button = clayConfig.getItemById('resetDefaults');
    var armed = null;
    function disarm() {
      if (armed) { clearTimeout(armed); armed = null; }
      button.set('Reset to defaults');
    }
    button.on('click', function () {
      if (!armed) {
        button.set('Tap again to confirm');
        armed = setTimeout(disarm, 4000);
        return;
      }
      disarm();
      clayConfig.getAllItems().forEach(function (it) {
        if (it.messageKey && it.config.defaultValue !== undefined) it.set(it.config.defaultValue);
      });
    });
  });
};
