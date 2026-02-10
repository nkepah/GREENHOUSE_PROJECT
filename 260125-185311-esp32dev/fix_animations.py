#!/usr/bin/env python3
import os
os.chdir('data')

with open('index.html', 'r', encoding='utf-8') as f:
    content = f.read()

# Find and replace animation logic
old_pattern = "const type=d.type.toLowerCase();const pwrData=devicePowerData[d.ch];const isHealthy=!pwrData||pwrData.healthy!==false;const deviceAmps=pwrData?pwrData.amps:0;if(!isHealthy){el.classList.add('anim-fault');}else if(type.includes('heat')||type==='heater'){el.classList.add('anim-fire');}else if(type.includes('fan_circ')||type.includes('circ')){el.classList.add('anim-orange');}else if(type.includes('fan_cool')||type.includes('cool')){el.classList.add('anim-blue');}else if(type.includes('vent')){el.classList.add('anim-vent');}else if(type.includes('light')||type.includes('lamp')||type.includes('grow')){el.classList.add('anim-white');}else if(type.includes('door')){el.classList.add('anim-door-open');}"

new_pattern = "const type=d.type.toLowerCase();const pwrData=devicePowerData[d.ch];const isHealthy=!pwrData||pwrData.healthy!==false;const deviceAmps=pwrData?pwrData.amps:0;const powerTrackingEnabled=document.getElementById('power-tracking-input')?.checked||false;if(!isHealthy){el.classList.add('anim-fault');}else if(powerTrackingEnabled){if(deviceAmps>0.20){if(type.includes('heat')||type==='heater'){el.classList.add('anim-fire');}else if(type.includes('fan_circ')||type.includes('circ')){el.classList.add('anim-orange');}else if(type.includes('fan_cool')||type.includes('cool')){el.classList.add('anim-blue');}else if(type.includes('vent')){el.classList.add('anim-vent');}else if(type.includes('light')||type.includes('lamp')||type.includes('grow')){el.classList.add('anim-white');}else if(type.includes('door')){el.classList.add('anim-door-open');}const ampBadge=document.createElement('div');ampBadge.className='current-badge';ampBadge.textContent=deviceAmps.toFixed(2)+'A';el.appendChild(ampBadge);}else{el.classList.add('anim-fault');const ampBadge=document.createElement('div');ampBadge.className='current-badge fault';ampBadge.textContent='0.0A';el.appendChild(ampBadge);}}else{if(type.includes('heat')||type==='heater'){el.classList.add('anim-fire');}else if(type.includes('fan_circ')||type.includes('circ')){el.classList.add('anim-orange');}else if(type.includes('fan_cool')||type.includes('cool')){el.classList.add('anim-blue');}else if(type.includes('vent')){el.classList.add('anim-vent');}else if(type.includes('light')||type.includes('lamp')||type.includes('grow')){el.classList.add('anim-white');}else if(type.includes('door')){el.classList.add('anim-door-open');}}"

if old_pattern in content:
    content = content.replace(old_pattern, new_pattern)
    with open('index.html', 'w', encoding='utf-8') as f:
        f.write(content)
    print("SUCCESS: Animation logic updated")
    exit(0)
else:
    print("ERROR: Could not find animation logic pattern")
    # Try to find what's actually there
    if 'const type=d.type.toLowerCase()' in content:
        print("Found type definition, showing context...")
        pos = content.find('const type=d.type.toLowerCase()')
        print(content[pos:pos+500])
    exit(1)
