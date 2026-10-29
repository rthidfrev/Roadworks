#!/usr/bin/env python3
"""Reproducible original monitor effects, with no sampled/licensed source audio."""
import hashlib, json, math, random, struct, wave
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
RATE = 48000
DEST = ROOT / 'art/road-monitor/audio'

def envelope(t, start, duration, attack=.01):
    u = t - start
    if u < 0 or u >= duration: return 0
    return min(1, u / attack) * min(1, (duration - u) / .025) * math.sin(math.pi * u / duration)**.6

def generate(name, duration, closing=False):
    rng = random.Random(8301 + int(closing))
    values, low, phase = [], 0., 0.
    for i in range(round(RATE * duration)):
        t = i / RATE
        motor_start, motor_length = (.11, .26) if closing else (.0, .32)
        u = max(0., min(1., (t - motor_start) / motor_length))
        frequency = (185 - 65*u) if closing else (125 + 70*math.sin(math.pi*u))
        phase += 2*math.pi*frequency/RATE
        noise = rng.uniform(-1, 1); low += .12*(noise-low)
        motor = envelope(t, motor_start, motor_length)
        value = motor*(.11*math.sin(phase) + .027*math.sin(phase*2.03) + .022*low)
        click_at = .375 if closing else .305
        d = t - click_at
        if d >= 0:
            value += .11*math.exp(-d/ .013)*math.sin(2*math.pi*640*d)
            value += .035*math.exp(-d/.021)*math.sin(2*math.pi*1280*d)
        if not closing:
            value += .035*envelope(t, .365, .075)*math.sin(2*math.pi*880*t)
            value += .028*envelope(t, .435, .080)*math.sin(2*math.pi*1320*t)
        else:
            value += .025*envelope(t, .0, .095)*math.sin(2*math.pi*(880*t-1900*t*t))
        # All endpoints are silent; short master fades avoid PCM boundary clicks.
        value *= min(1, t/.006, (duration-t)/.014)
        values.append(value)
    scale=.42/max(abs(x) for x in values)
    pcm=[round(x*scale*32767) for x in values]; pcm[0]=pcm[-1]=0
    DEST.mkdir(parents=True,exist_ok=True); path=DEST/(name+'.wav')
    with wave.open(str(path),'wb') as out:
        out.setnchannels(1);out.setsampwidth(2);out.setframerate(RATE)
        out.writeframes(struct.pack('<'+'h'*len(pcm),*pcm))
    peak=max(abs(x) for x in pcm)/32768
    rms=math.sqrt(sum((x/32768)**2 for x in pcm)/len(pcm))
    assert peak < .43 and pcm[0]==pcm[-1]==0 and 0.01 < rms < .15
    return {'file':str(path.relative_to(ROOT)),'seconds':duration,'frames':len(pcm),'rate':RATE,
        'channels':1,'peak':peak,'rms':rms,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}

if __name__ == '__main__':
    clips=[generate('RoadMonitorDeploy',.54),generate('RoadMonitorRetract',.43,True)]
    report={'origin':'Original deterministic synthesis for Roadworks; no third-party recordings', 'clips':clips}
    (DEST/'audio-source.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
