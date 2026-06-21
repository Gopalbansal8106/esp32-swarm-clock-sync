import os
import glob
import pandas as pd
import numpy as np

logs_dir = "logs"
csv_files = glob.glob(os.path.join(logs_dir, "*.csv"))

df_list = []
for f in csv_files:
    try:
        df_list.append(pd.read_csv(f))
    except Exception as e:
        print(f"Error reading {f}: {e}")

if not df_list:
    print("No CSV files found.")
    exit()

df = pd.concat(df_list, ignore_index=True)
df['PC_Timestamp'] = pd.to_datetime(df['PC_Timestamp'], errors='coerce')
df = df.sort_values('PC_Timestamp').reset_index(drop=True)

# Some older logs might use 'Offset_us' and 'Peer_Offset_us'. Let's normalize it to 'Raw_Offset_us'.
if 'Raw_Offset_us' not in df.columns and 'Offset_us' in df.columns:
    df['Raw_Offset_us'] = df['Offset_us']

# Keep only data under 500ms (500,000 microseconds)
# Use absolute value for filtering outliers
if 'Raw_Offset_us' in df.columns:
    df = df[df['Raw_Offset_us'].abs() <= 500000]

print("=== OVERALL STATISTICS ===")
# Separate 1-way ('basic') and 2-way ('espnow')
df_1way = df[df['Sync_Mode'] == 'basic']
df_2way = df[df['Sync_Mode'] == 'espnow']

print(f"1-Way Sync (basic):")
print(f"  Count: {len(df_1way)}")
print(f"  Average Offset: {df_1way['Raw_Offset_us'].mean() / 1000.0:.3f} ms")
print(f"  Standard Deviation: {df_1way['Raw_Offset_us'].std() / 1000.0:.3f} ms")

print(f"\n2-Way Sync (espnow):")
print(f"  Count: {len(df_2way)}")
print(f"  Average Offset: {df_2way['Raw_Offset_us'].mean() / 1000.0:.3f} ms")
print(f"  Standard Deviation: {df_2way['Raw_Offset_us'].std() / 1000.0:.3f} ms")

print("\n=== LEADER TRACKING ===")
# To track leader switches, we look at the Leader_Node column
if 'Leader_Node' in df.columns:
    leader_col = 'Leader_Node'
elif 'Is_Leader' in df.columns:
    leader_col = 'Is_Leader'
else:
    leader_col = None

if leader_col:
    # Identify contiguous blocks where the leader is the same
    df['Leader_Change'] = df[leader_col].shift() != df[leader_col]
    switches = df[df['Leader_Change']]
    
    num_switches = len(switches) - 1 # first row is marked as change
    print(f"Total Leader Switches: {max(0, num_switches)}")
    
    # Calculate duration of each leader
    df['Block'] = df['Leader_Change'].cumsum()
    blocks = df.groupby('Block').agg({
        leader_col: 'first',
        'PC_Timestamp': ['min', 'max']
    })
    blocks['Duration'] = (blocks['PC_Timestamp']['max'] - blocks['PC_Timestamp']['min']).dt.total_seconds()
    
    print("\nLeader Switch History:")
    for idx, row in blocks.iterrows():
        leader = row[leader_col]['first']
        start_time = row['PC_Timestamp']['min']
        duration = row['Duration'].values[0]
        # print(f"  Leader: {leader} | Started at: {start_time} | Duration: {duration:.1f} seconds")

print("\n=== ADVANCED METRICS ===")

# 1. 99th Percentile Error and Max Spike
print("\n[Error Extremes]")
print(f"1-Way 99th Percentile Error: {df_1way['Raw_Offset_us'].abs().quantile(0.99) / 1000.0:.3f} ms")
print(f"2-Way 99th Percentile Error: {df_2way['Raw_Offset_us'].abs().quantile(0.99) / 1000.0:.3f} ms")
print(f"1-Way Max Spike: {df_1way['Raw_Offset_us'].abs().max() / 1000.0:.3f} ms")
print(f"2-Way Max Spike: {df_2way['Raw_Offset_us'].abs().max() / 1000.0:.3f} ms")

# 2. Drift Stability (PPM)
print("\n[Clock Drift Stability (PPM)]")
print(f"1-Way Drift StdDev: {df_1way['Drift_PPM'].std():.3f} PPM")
print(f"2-Way Drift StdDev: {df_2way['Drift_PPM'].std():.3f} PPM")

# 3. Overall Packet Loss
if 'Total_Sent' in df.columns and 'Total_Lost' in df.columns:
    max_sent = df['Total_Sent'].max()
    max_lost = df['Total_Lost'].max()
    if max_sent > 0:
        loss_rate = (max_lost / max_sent) * 100.0
        print(f"\n[Network Reliability]")
        print(f"Overall Packet Loss Rate: {loss_rate:.2f}%")

# 4. RSSI Impact on Error (Only for peers where RSSI != 0)
if 'RSSI_dBm' in df.columns:
    df_peers = df[df['RSSI_dBm'] < 0] # Filter out self (0 dBm)
    if not df_peers.empty:
        good_signal = df_peers[df_peers['RSSI_dBm'] >= -60]
        weak_signal = df_peers[df_peers['RSSI_dBm'] < -60]
        
        print("\n[Signal Strength Impact]")
        if not good_signal.empty:
            print(f"Avg Error (Strong Signal >= -60dBm): {good_signal['Raw_Offset_us'].abs().mean() / 1000.0:.3f} ms")
        if not weak_signal.empty:
            print(f"Avg Error (Weak Signal < -60dBm): {weak_signal['Raw_Offset_us'].abs().mean() / 1000.0:.3f} ms")

# 5. Synchronization Uptime (SLA)
if 'Is_Synced' in df.columns:
    uptime_1way = (df_1way['Is_Synced'] == True).mean() * 100.0
    uptime_2way = (df_2way['Is_Synced'] == True).mean() * 100.0
    print("\n[Synchronization Uptime (SLA)]")
    print(f"1-Way System Uptime: {uptime_1way:.2f}% of the time")
    print(f"2-Way System Uptime: {uptime_2way:.2f}% of the time")

else:
    print("Leader tracking column not found.")
