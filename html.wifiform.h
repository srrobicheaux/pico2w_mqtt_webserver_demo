#define RESPOND_CONFIG R"RAWHTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>BatMon | Configuration</title>
    <style>
        :root {
            --bg-color: #0e1012;
            --card-bg: #1a1d21;
            --text-primary: #ffffff;
            --text-secondary: #9ca3af;
            --accent-blue: #3b82f6;
            --accent-green: #10b981;
            --accent-orange: #f59e0b;
            --border-color: #374151;
            --input-bg: #262a30;
        }

        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: system-ui, -apple-system, sans-serif; background: var(--bg-color); color: var(--text-primary); padding: 20px; line-height: 1.5; }
        .container { max-width: 800px; margin: 0 auto; }
        
        header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 30px; border-bottom: 1px solid var(--border-color); padding-bottom: 15px; }
        h1 { font-size: 1.5rem; color: var(--accent-orange); }
        .nav-link { color: var(--text-secondary); text-decoration: none; font-size: 0.9rem; }
        .nav-link:hover { color: var(--accent-blue); }

        section { background: var(--card-bg); border-radius: 12px; padding: 20px; border: 1px solid var(--border-color); margin-bottom: 20px; }
        
        .section-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; border-left: 4px solid var(--accent-blue); padding-left: 10px; }
        h2 { font-size: 1rem; text-transform: uppercase; letter-spacing: 1px; color: var(--text-secondary); }
        
        .count-ctrl { display: flex; align-items: center; gap: 10px; background: rgba(255,255,255,0.05); padding: 5px 10px; border-radius: 6px; }
        .count-ctrl label { font-size: 0.75rem; color: var(--text-secondary); }
        .count-ctrl input { width: 60px; padding: 5px; text-align: center; }

        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
        .form-group { display: flex; flex-direction: column; gap: 8px; }
        label { font-size: 0.85rem; font-weight: 600; color: var(--text-secondary); }
        
        input[type="text"], input[type="number"], select {
            background: var(--input-bg);
            border: 1px solid var(--border-color);
            border-radius: 6px;
            padding: 10px;
            color: var(--text-primary);
            font-size: 0.9rem;
            outline: none;
        }
        input:focus { border-color: var(--accent-blue); }
        input[readonly] { opacity: 0.5; cursor: not-allowed; }

        .io-row { 
            display: grid; 
            grid-template-columns: 50px 1fr 80px 80px 80px; 
            gap: 10px; 
            align-items: center; 
            padding: 10px 0;
            border-bottom: 1px solid rgba(255,255,255,0.05);
            transition: opacity 0.3s ease;
        }
        .pio-row { grid-template-columns: 50px 1fr 100px 100px; }
        .io-header { font-weight: bold; font-size: 0.75rem; color: var(--text-secondary); border-bottom: 1px solid var(--border-color); padding-bottom: 5px; }
        
        /* Checkbox styling */
        input[type="checkbox"] { width: 18px; height: 18px; accent-color: var(--accent-blue); cursor: pointer; }

        .btn-save {
            background: var(--accent-blue);
            color: white;
            border: none;
            padding: 15px 30px;
            border-radius: 8px;
            font-weight: bold;
            cursor: pointer;
            width: 100%;
            font-size: 1rem;
            margin-top: 10px;
            transition: opacity 0.2s;
        }
        .btn-save:hover { opacity: 0.9; }
        .status-msg { text-align: center; margin-top: 15px; font-size: 0.9rem; display: none; }

        @media (max-width: 600px) { 
            .grid { grid-template-columns: 1fr; } 
            .io-row { grid-template-columns: 1fr 1fr; gap: 15px; } 
            .section-header { flex-direction: column; align-items: flex-start; gap: 10px; }
        }
    </style>
</head>
<body>

<div class="container">
    <header>
        <h1>System Config</h1>
        <a href="/" class="nav-link">← Back to Dashboard</a>
    </header>

    <form id="configForm">
        <!-- WIFI & SYSTEM -->
        <section>
            <div class="section-header"><h2>Network & Device</h2></div>
            <div class="grid">
                <div class="form-group">
                    <label>WiFi SSID</label>
                    <input type="text" id="wifi_ssid">
                </div>
                <div class="form-group">
                    <label>Device ID (Read Only)</label>
                    <input type="text" id="wifi_dev_id" readonly>
                </div>
                <div class="form-group">
                    <label>WiFi PW</label>
                    <input type="text" id="wifi_password">
                </div>
                <div class="form-group">
                    <label>MQTT PW</label>
                    <input type="text" id="mqtt_password">
                </div>
                <div class="form-group">
                    <label>MQTT Server</label>
                    <input type="text" id="mqtt_server">
                </div>
                <div class="form-group">
                    <label>MQTT User</label>
                    <input type="text" id="mqtt_user">
                </div>
            </div>
        </section>

        <!-- ANALOG CHANNELS -->
        <section>
            <div class="section-header">
                <h2>Analog Inputs</h2>
                <div class="count-ctrl">
                    <label>Active Count:</label>
                    <input type="number" id="analog_count" min="0" max="32">
                </div>
            </div>
            <div class="io-row io-header">
                <div>En</div><div>Label Name</div><div>Ratio</div><div>Offset</div>
            </div>
            <div id="analog-list">
                <!-- Generated by JS -->
            </div>
        </section>

        <!-- PIO PINS -->
        <section>
            <div class="section-header">
                <h2>Digital Pins (PIO)</h2>
                <div class="count-ctrl">
                    <label>Active Count:</label>
                    <input type="number" id="pio_count" min="0" max="32">
                </div>
            </div>
            <div class="pio-row io-row io-header">
                <div>En</div><div>Pin Label</div><div>Direction</div>
            </div>
            <div id="pio-list">
                <!-- Generated by JS -->
            </div>
        </section>
        <div id="status" class="status-msg"></div>
        <button type="submit" class="btn-save">Save Settings</button>
    </form>
</div>

<script>
    let currentSettings = {};

    function highlightActivePins() {
        const aCount = parseInt(document.getElementById('analog_count').value) || 0;
        document.querySelectorAll('#analog-list .io-row').forEach((row, i) => {
            row.style.opacity = (i < aCount) ? '1' : '0.3';
        });

        const pCount = parseInt(document.getElementById('pio_count').value) || 0;
        document.querySelectorAll('#pio-list .pio-row').forEach((row, i) => {
            row.style.opacity = (i < pCount) ? '1' : '0.3';
        });
    }

    // Bind event listeners to visually update when the user changes counts
    document.getElementById('analog_count').addEventListener('input', highlightActivePins);
    document.getElementById('pio_count').addEventListener('input', highlightActivePins);

    // 1. Fetch current settings and fill the form
    fetch('./settings')
        .then(res => res.json())
        .then(data => {
            currentSettings = data;
            
            // Map Basic Info
            if(data.wifi) {
                document.getElementById('wifi_ssid').value = data.wifi.ssid || '';
                document.getElementById('wifi_password').value = data.wifi.password || '';
                document.getElementById('wifi_dev_id').value = data.wifi.dev_id || '';
            }
            if(data.mqtt) {
                document.getElementById('mqtt_server').value = data.mqtt.server || '';
                document.getElementById('mqtt_user').value = data.mqtt.user || '';
                document.getElementById('mqtt_password').value = data.mqtt.password || '';
            }

            if(data.IOs) {
                // Set Counts
                document.getElementById('analog_count').value = data.IOs.analog_count || 0;
                document.getElementById('pio_count').value = data.IOs.pio_count || 0;

                // Generate Analog Rows
                if(data.IOs.analog) {
                    const aList = document.getElementById('analog-list');
                    data.IOs.analog.forEach((a, i) => {
                        const row = document.createElement('div');
                        row.className = 'io-row';
                        row.innerHTML = `
                            <input type="checkbox" class="a-en" ${a.enabled ? 'checked' : ''}>
                            <input type="text" class="a-name" value="${a.name || ''}" placeholder="Name">
                            <input type="number" step="0.001" class="a-ratio" value="${a.ratio || 0}">
                            <input type="number" step="0.001" class="a-offset" value="${a.offset || 0}">
                        `;
                        aList.appendChild(row);
                    });
                }

                // Generate PIO Rows
                if(data.IOs.pio) {
                    const pList = document.getElementById('pio-list');
                    data.IOs.pio.forEach((p, i) => {
                        const row = document.createElement('div');
                        row.className = 'io-row pio-row';
                        row.innerHTML = `
                            <input type="checkbox" class="p-en" ${p.enabled ? 'checked' : ''}>
                            <input type="text" class="p-name" value="${p.name || ''}" placeholder="Pin ${i}">
                            <select class="p-out">
                                <option value="false" ${!p.out ? 'selected' : ''}>Input</option>
                                <option value="true" ${p.out ? 'selected' : ''}>Output</option>
                            </select>
                        `;
                        pList.appendChild(row);
                    });
                }
            }
            
            // Run initially to dim inactive rows
            highlightActivePins();
        });

    // 2. Collect and Save
    document.getElementById('configForm').onsubmit = (e) => {
        e.preventDefault();
        const status = document.getElementById('status');
        status.style.display = 'block';
        status.textContent = 'Saving...';
        status.style.color = 'var(--accent-orange)';

        // Safely update current settings
        if(currentSettings.wifi) currentSettings.wifi.ssid = document.getElementById('wifi_ssid').value;
        if(currentSettings.wifi) currentSettings.wifi.password = document.getElementById('wifi_password').value;

        if(currentSettings.mqtt) {
            currentSettings.mqtt.server = document.getElementById('mqtt_server').value;
            currentSettings.mqtt.user = document.getElementById('mqtt_user').value;
            currentSettings.mqtt.password = document.getElementById('mqtt_password').value;
        }

        if(currentSettings.IOs) {
            currentSettings.IOs.analog_count = parseInt(document.getElementById('analog_count').value) || 0;
            currentSettings.IOs.pio_count = parseInt(document.getElementById('pio_count').value) || 0;

            // Re-map Analog: Mutating existing objects preserves deep struct integrity
            const aRows = document.querySelectorAll('#analog-list .io-row');
            Array.from(aRows).forEach((row, i) => {
                if(currentSettings.IOs.analog[i]) {
                    currentSettings.IOs.analog[i].enabled = row.querySelector('.a-en').checked;
                    currentSettings.IOs.analog[i].name = row.querySelector('.a-name').value;
                    currentSettings.IOs.analog[i].ratio = parseFloat(row.querySelector('.a-ratio').value) || 0;
                    currentSettings.IOs.analog[i].offset = parseFloat(row.querySelector('.a-offset').value) || 0;
                }
            });

            // Re-map PIO
            const pRows = document.querySelectorAll('#pio-list .pio-row');
            Array.from(pRows).forEach((row, i) => {
                if(currentSettings.IOs.pio[i]) {
                    currentSettings.IOs.pio[i].enabled = row.querySelector('.p-en').checked;
                    currentSettings.IOs.pio[i].name = row.querySelector('.p-name').value;
                    currentSettings.IOs.pio[i].out = (row.querySelector('.p-out').value === 'true');
                }
            });
        }

        // Post back to server
        fetch('./save_settings', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(currentSettings)
        })
        .then(res => {
            if(res.ok) {
                status.textContent = 'Settings Saved Successfully!';
                status.style.color = 'var(--accent-green)';
            } else {
                throw new Error('Server returned ' + res.status);
            }
        })
        .catch((err) => {
            console.error(err);
            status.textContent = 'Error saving settings.';
            status.style.color = 'var(--accent-red)';
        });
    };
</script>

</body>
</html>
)RAWHTML"