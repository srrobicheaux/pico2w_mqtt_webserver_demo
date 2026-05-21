#define RESPOND_DASHBOARD R"RAWHTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>BatMon | Dashboard</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        :root {
            --bg-color: #0e1012;
            --card-bg: #1a1d21;
            --text-primary: #ffffff;
            --text-secondary: #9ca3af;
            --accent-blue: #3b82f6;
            --accent-green: #10b981;
            --accent-purple: #8b5cf6;
            --accent-orange: #f59e0b;
            --accent-red: #ef4444;
            --border-color: #374151;
        }

        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: system-ui, -apple-system, sans-serif; background: var(--bg-color); color: var(--text-primary); padding: 20px; display: flex; justify-content: center; }
        .container { width: 100%; max-width: 1200px; display: grid; grid-template-columns: repeat(3, 1fr); gap: 20px; }
        
        header { 
            grid-column: 1 / -1; 
            display: flex; 
            justify-content: space-between; 
            align-items: center; 
            margin-bottom: 5px; 
            flex-wrap: wrap;
            gap: 15px;
        }
        
        .header-stats {
            display: flex;
            align-items: center;
            gap: 15px;
            font-family: monospace;
            font-size: 0.85rem;
            color: var(--text-secondary);
            background: rgba(255,255,255,0.05);
            padding: 6px 12px;
            border-radius: 8px;
            border: 1px solid var(--border-color);
        }
        .sys-item { display: flex; gap: 6px; align-items: center; }
        .sys-val { color: var(--text-primary); font-weight: 600; }
        
        #sys-temp { display: inline-block; min-width: 55px; text-align: right; }
        
        .live-indicator { font-size: 0.8rem; color: var(--text-secondary); display: flex; align-items: center; gap: 6px; font-family: monospace; }
        .dot { width: 10px; height: 10px; background-color: var(--border-color); border-radius: 50%; transition: background-color 0.3s, box-shadow 0.3s; }
        .dot.active { background-color: var(--accent-green); box-shadow: 0 0 8px var(--accent-green); }

        .card { 
            background: var(--card-bg); 
            border-radius: 16px; 
            padding: 20px; 
            border: 1px solid var(--border-color); 
            display: flex; 
            flex-direction: column; 
            position: relative;
            transition: background-color 0.5s ease, border-color 0.5s ease;
        }
        .card-label { font-size: 0.75rem; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 1px; margin-bottom: 4px; }
        .card-value { font-size: 1.8rem; font-weight: 700; font-family: monospace; }

        .gauge-container { width: 100%; max-width: 140px; margin: 5px auto 10px auto; }
        .gauge path.bg-arc { stroke: var(--border-color); }
        .gauge line.needle {
            transform-origin: 50px 45px;
            transition: transform 0.4s cubic-bezier(0.17, 0.67, 0.83, 0.67);
            stroke: var(--accent-red);
        }

        .full-width { grid-column: 1 / -1; }
        .chart-container { min-height: 300px; }
        .unit { font-size: 0.9rem; color: var(--text-secondary); margin-left: 4px; }
        
        .config-btn {
            color: var(--text-primary);
            text-decoration: none;
            font-size: 0.85rem;
            font-weight: 600;
            background: rgba(255,255,255,0.05);
            border: 1px solid var(--border-color);
            padding: 6px 12px;
            border-radius: 8px;
            transition: background 0.2s, border-color 0.2s;
        }
        .config-btn:hover { background: rgba(255,255,255,0.1); border-color: var(--accent-blue); }

        .gpio-badge {
            display: flex;
            align-items: center;
            gap: 8px;
            background: rgba(255,255,255,0.05);
            padding: 6px 12px;
            border-radius: 8px;
            border: 1px solid var(--border-color);
            font-size: 0.75rem;
            font-weight: 600;
            font-family: monospace;
            transition: all 0.2s ease;
        }

        /* Hover styles for interactive pins */
        .gpio-badge.clickable { cursor: pointer; }
        .gpio-badge.clickable:hover { background: rgba(59, 130, 246, 0.15); border-color: var(--accent-blue); }

        @media (max-width: 900px) { .container { grid-template-columns: repeat(2, 1fr); } }
        @media (max-width: 600px) { 
            .container { grid-template-columns: 1fr; }
            .header-stats { order: 3; width: 100%; justify-content: center; } 
        }
    </style>
</head>
<body>

    <div class="container">
        <header>
            <div style="display: flex; flex-direction: column; gap: 4px;">
                <h2 id="dev-model" style="font-size: 1.2rem; color: var(--accent-orange);">BatMon</h2>
                <span id="dev-id" style="font-size: 0.65rem; color: var(--text-secondary); font-family: monospace;">ID: --</span>
            </div>
            
            <div class="header-stats">
                <div class="sys-item"><span>CPU:</span><span id="sys-temp" class="sys-val">--°F</span></div>
                <div class="sys-item"><span>VER:</span><span id="sys-ver" class="sys-val">--</span></div>
                <a href="/config" class="config-btn">Settings</a>
            </div>

            <div class="live-indicator">
                <span id="rate-value">0 eps</span>
                <div class="dot" id="conn-dot"></div> 
                <span id="conn-text">Connecting...</span>
            </div>
        </header>

        <div class="card full-width" id="gpio-container" style="display: none;">
            <div class="card-label">Digital Status</div>
            <div id="gpio-badges" style="display: flex; gap: 10px; flex-wrap: wrap; margin-top: 8px;"></div>
        </div>

        <!-- Dynamic Analog Cards Will Be Injected Here -->
        <span id="analog-anchor" style="display: none;"></span>

        <div class="card full-width chart-container">
            <div class="card-label">Voltage History</div>
            <div style="position: relative; flex-grow: 1; height: 100%;">
                <canvas id="voltageChart"></canvas>
            </div>
        </div>
    </div>

<script>
    const MAX_POINTS = 60;
    let frameCount = 0;
    let lastRateCheck = Date.now();
    let lastChartUpdate = 0;
    
    // Global function to toggle PIO states
    function togglePio(pioIndex) {
        fetch(`/pio=${pioIndex}/toggle`)
            .then(res => res.json())
            .then(data => console.log(`Toggled PIO ${pioIndex}:`, data))
            .catch(err => console.error(`Toggle error on PIO ${pioIndex}:`, err));
    }

    const ctx = document.getElementById('voltageChart').getContext('2d');
    const chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: Array(MAX_POINTS).fill(''),
            datasets: [] // Start empty, populate via settings
        },
        options: {
            responsive: true, maintainAspectRatio: false, animation: false,
            scales: { y: { min: 0, max: 16, grid: { color: '#374151' } }, x: { display: false } },
            plugins: { legend: { labels: { color: '#9ca3af' } } }
        }
    });

    // Chart line colors to cycle through dynamically
    const chartColors = ['#3b82f6', '#10b981', '#8b5cf6', '#f59e0b', '#ef4444', '#06b6d4'];

    // --- LOAD SETTINGS ---
    fetch('./settings')
        .then(res => res.json())
        .then(data => {
            if(data.mqtt) {
                document.getElementById('dev-model').textContent = data.mqtt.model || 'BatMon';
                document.getElementById('sys-ver').textContent = data.mqtt.version || '--';
            }
            if(data.wifi) document.getElementById('dev-id').textContent = 'ID: ' + data.wifi.dev_id;

            if(data.IOs) {
                const aCount = data.IOs.analog_count || 0;
                const pCount = data.IOs.pio_count || 0;

                // Build Dynamic Analog Cards & Chart Datasets
                if(data.IOs.analog) {
                    let analogHtml = '';
                    let newDatasets = [];
                    let dSetColorIdx = 0;
                    
                    // Only loop up to the reported count
                    for(let i = 0; i < aCount && i < data.IOs.analog.length; i++) {
                        const a = data.IOs.analog[i];
                        if(a && a.enabled) {
                            const name = a.name || `Analog ${i}`;
                            const color = chartColors[dSetColorIdx % chartColors.length];
                            
                            // Build Card UI
                            analogHtml += `
                            <div class="card">
                                <div class="card-label">${name}</div>
                                <div class="gauge-container">
                                    <svg viewBox="0 0 100 50" class="gauge">
                                        <path class="bg-arc" d="M 10 45 A 40 40 0 0 1 90 45" fill="none" stroke-width="6" stroke-linecap="round"/>
                                        <line class="needle" id="v${i}-needle" x1="50" y1="45" x2="50" y2="10" stroke-width="2" style="transform: rotate(-90deg);"/>
                                        <circle cx="50" cy="45" r="4" fill="var(--accent-red)"/>
                                    </svg>
                                </div>
                                <div><span class="card-value" id="v${i}-val">--.--</span><span class="unit">V</span></div>
                            </div>`;

                            // Build Dataset. We save original index '_idx' so SSE knows where to put incoming array data
                            newDatasets.push({
                                label: name,
                                borderColor: color,
                                data: Array(MAX_POINTS).fill(null),
                                borderWidth: 2, pointRadius: 0, tension: 0.3,
                                _idx: i 
                            });
                            dSetColorIdx++;
                        }
                    }
                    
                    if(analogHtml) {
                        // Insert dynamically rendered cards just before the anchor point
                        document.getElementById('analog-anchor').insertAdjacentHTML('beforebegin', analogHtml);
                        chart.data.datasets = newDatasets;
                        chart.update();
                    }
                }
                
                // Build PIO Badges
                if(data.IOs.pio) {
                    let html = '';
                    
                    // Only loop up to the reported count
                    for(let i = 0; i < pCount && i < data.IOs.pio.length; i++) {
                        const p = data.IOs.pio[i];
                        if(p && p.enabled) {
                            const name = p.name || `P${i}`;
                            
                            // Check if set to 'in' (out == false)
                            const isInput = (p.out === false);
                            const clickAttr = isInput ? `onclick="togglePio(${i})" title="Toggle GPIO ${i}"` : `title="Output Pin"`;
                            const badgeClass = isInput ? 'gpio-badge clickable' : 'gpio-badge';
                            
                            html += `<div class="${badgeClass}" ${clickAttr}><div class="dot" id="pio-dot-${i}"></div><span>${name}</span></div>`;
                        }
                    }
                    if(html) {
                        document.getElementById('gpio-badges').innerHTML = html;
                        document.getElementById('gpio-container').style.display = 'block';
                    }
                }
            }
        });

    const evtSource = new EventSource('/events');
    const connDot = document.getElementById('conn-dot');
    const connText = document.getElementById('conn-text');

    evtSource.onopen = () => { connDot.classList.add('active'); connText.textContent = "Live"; };

    evtSource.onmessage = (e) => {
        try {
            const data = JSON.parse(e.data);
            frameCount++;
            
            if (data.system_status) {
                const temp = data.system_status.temperature;
                if(temp !== undefined) document.getElementById('sys-temp').textContent = temp.toFixed(1) + "°F";
            }

            if (data.IO) {
                // Handle PIO Array
                if (data.IO.pio) {
                    data.IO.pio.forEach((state, i) => {
                        const d = document.getElementById(`pio-dot-${i}`);
                        if (d) state ? d.classList.add('active') : d.classList.remove('active');
                    });
                }

                // Handle Analog Array by matching dataset to incoming JSON index
                if (data.IO.analog) {
                    chart.data.datasets.forEach((ds) => {
                        const idx = ds._idx;
                        if (idx !== undefined && data.IO.analog[idx] !== undefined) {
                            const val = data.IO.analog[idx];
                            ds.data.push(val);
                            ds.data.shift();
                            
                            const valEl = document.getElementById(`v${idx}-val`);
                            if (valEl) {
                                valEl.textContent = val.toFixed(2);
                                const angle = Math.max(-90, Math.min(90, (val / 16) * 180 - 90));
                                document.getElementById(`v${idx}-needle`).style.transform = `rotate(${angle}deg)`;
                            }
                        }
                    });
                }
            }

            if (Date.now() - lastChartUpdate > 250) {
                chart.update();
                lastChartUpdate = Date.now();
            }

            if (Date.now() - lastRateCheck >= 1000) {
                document.getElementById('rate-value').textContent = frameCount + " eps";
                frameCount = 0;
                lastRateCheck = Date.now();
            }
        } catch (err) {}
    };

    evtSource.onerror = () => { connDot.classList.remove('active'); connText.textContent = "Offline"; };
</script>
</body>
</html>
)RAWHTML"