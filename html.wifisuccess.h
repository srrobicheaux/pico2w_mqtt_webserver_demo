//wifisucces.html.h
#define RESPOND_SUCCESS R"RAWHTML(<!DOCTYPE html>
<html><head><title>Success</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
    body{font-family:sans-serif;text-align:center;margin:40px;background:#f4f4f9;color:#333;}
    .timer{font-size:48px;font-weight:bold;color:#0078d4;margin:20px;}
    .loader{border:6px solid #f3f3f3;border-top:6px solid #0078d4;border-radius:50%;width:40px;height:40px;animation:spin 2s linear infinite;display:inline-block;}
    @keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}
    </style></head>
    <body>
    <h1>Configuration Saved!</h1>
    <p>Connecting to your WiFi...<br>The AP will disappear if successful.</p>
    <div class="loader"></div>
    <div class="timer" id="countdown">30</div><p>Redirecting to dashboard in <span id="seconds">30 
    </span> seconds...</p>
    <script>
    var seconds =  30;
    var countdown = document.getElementById('countdown');
    var secText = document.getElementById('seconds');
    var timer = setInterval(function() {
    seconds--;
    countdown.textContent = seconds;
    secText.textContent = seconds;
    if (seconds <= 0) {
    clearInterval(timer);
    window.location.href = ' DASHBOARD ';
    }
    }, 1000);
    </script>
    </body></html>
)RAWHTML"
