# -*- coding: utf-8 -*-

def nmea_crc(sentence):
    c = 0
    for ch in sentence:
        if ch == '*':
            break
        c ^= ord(ch)
    return c

def lat_to_nmea(deg):
    abs_deg = abs(deg)
    d = int(abs_deg)
    m = (abs_deg - d) * 60.0
    ns = 'N' if deg >= 0 else 'S'
    return '{:02d}{:09.6f}'.format(d, m), ns

def lon_to_nmea(deg):
    abs_deg = abs(deg)
    d = int(abs_deg)
    m = (abs_deg - d) * 60.0
    ew = 'E' if deg >= 0 else 'W'
    return '{:03d}{:09.6f}'.format(d, m), ew

def make_rmc(utc, lat, lon, speed_kn, track):
    lat_s, ns = lat_to_nmea(lat)
    lon_s, ew = lon_to_nmea(lon)
    body = 'GNRMC,{},A,{},{},{},{},{},{},060624,,,A'.format(
        utc, lat_s, ns, lon_s, ew, speed_kn, track)
    chk = nmea_crc(body)
    return '${}*{:02X}'.format(body, chk)

def make_gga(utc, lat, lon, fix_qual, sats, alt):
    lat_s, ns = lat_to_nmea(lat)
    lon_s, ew = lon_to_nmea(lon)
    body = 'GNGGA,{},{},{},{},{},{},{},1.0,{},M,,M,,,'.format(
        utc, lat_s, ns, lon_s, ew, fix_qual, sats, alt)
    chk = nmea_crc(body)
    return '${}*{:02X}'.format(body, chk)

def make_gsv(total_msgs, msg_num, total_sats, satellites):
    """satellites: list of (prn, elev, azim, snr) tuples, max 4 per msg"""
    fields = [str(total_msgs), str(msg_num), str(total_sats)]
    for prn, elev, azim, snr in satellites:
        fields += [str(prn), str(elev), str(azim), str(snr)]
    body = 'GNGSV,{}'.format(','.join(fields))
    chk = nmea_crc(body)
    return '${}*{:02X}'.format(body, chk)

# Test scenarios with SNR values
# SNR thresholds: >42=EXCELLENT, >35=GOOD, >28=FAIR, >20=WEAK, <=20=NONE
scenarios = [
    # (speed_kmh, fix_qual, gga_sats, lat, lon, track, alt, snr_list, snr_label)
    (0,   0,  0, 40.07897925, 116.23662400,   0.0, 68.38, [ 0, 0, 0, 0, 0, 0, 0, 0], 'NONE(0)'),
    (10,  1,  8, 40.07897930, 116.23662410,  90.5, 68.40, [22,19,18,24,21,20,17,19], 'WEAK(1)'),
    (36,  1, 14, 40.07897950, 116.23662450,  91.0, 68.42, [30,32,28,35,31,29,33,30,34,31,29,32,30,28], 'FAIR(2)'),
    (72,  4, 22, 40.07898000, 116.23662500,  92.0, 68.45, [38,40,36,42,39,37,41,40,38,39,36,40,41,37,39,38,40,36,39,37,38,40], 'GOOD(3)'),
    (99,  4, 30, 40.07898100, 116.23662600,  95.0, 68.50, [45,48,43,50,47,44,46,49,45,47,43,48,46,50,44,47,49,45,48,46,50,43,47,49,45,48,46,50,44,47], 'EXCELLENT(4)'),
    (85,  4, 31, 40.07898200, 116.23662700, 180.0, 68.48, [46,47,44,49,46,45,48,47,49,45,46,47,44,48,49,46,45,47,48,44,46,49,45,47,48,46,44,49,45,47,46], 'EXCELLENT(4)'),
    (55,  5, 24, 40.07898300, 116.23662800, 270.0, 68.43, [37,35,33,38,36,34,39,35,37,33,38,36,34,35,37,33,38,36,34,39,35,37,33,38], 'GOOD(3)'),
    (24,  1, 15, 40.07898350, 116.23662850, 359.0, 68.40, [31,33,29,32,34,30,31,33,29,32,30,31,33,29,32], 'FAIR(2)'),
]

def signal_label(fq, sats):
    if fq == 0: return 'NONE(0)'
    if fq in (1, 2): return 'FAIR(2)' if sats >= 10 else 'WEAK(1)'
    if fq == 4: return 'EXCELLENT(4)'
    if fq == 5: return 'GOOD(3)'
    return 'FAIR(2)'

print('# ================================================================')
print('# UM982 NMEA Test: GNRMC + GNGGA + GNGSV triplets')
print('# Baud: 115200 8N1, send each line with CR+LF')
print('# SNR thresholds: >42=EXCELLENT, >35=GOOD, >28=FAIR, >20=WEAK')
print('# ================================================================')
print()

base_utc = 160000.00
for i, (speed_kmh, fq, sats, lat, lon, track, alt, snrs, snr_label) in enumerate(scenarios):
    utc = '{:09.2f}'.format(base_utc + i * 2.0)
    speed_kn = '{:.1f}'.format(speed_kmh / 1.852)
    track_s = '{:.1f}'.format(track)
    alt_s = '{:.1f}'.format(alt)

    avg_snr = sum(snrs) // len(snrs) if snrs and snrs[0] > 0 else 0
    gga_sig = signal_label(fq, sats)

    rmc = make_rmc(utc, lat, lon, speed_kn, track_s)
    gga = make_gga(utc, lat, lon, fq, sats, alt_s)

    print('# [{:d}] speed={:>3d}km/h  avgSNR={}dB->{}  GGA_sig={}'.format(
        i+1, speed_kmh, avg_snr, snr_label, gga_sig))

    print(rmc)
    print(gga)

    # Generate GSV messages (at most 4 sats per message)
    sat_entries = []
    for j, snr in enumerate(snrs):
        sat_entries.append((j+1, 30+j%60, (j*30)%360, snr))

    total_msgs = (len(sat_entries) + 3) // 4
    for m in range(total_msgs):
        block = sat_entries[m*4:(m+1)*4]
        gsv = make_gsv(total_msgs, m+1, len(sat_entries), block)
        print(gsv)
    print()

print('# ================================================================')
print('# Send RMC+GGA+GSV lines together for each test step.')
print('# ================================================================')
