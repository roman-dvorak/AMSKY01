#!/usr/bin/env python3
"""
AMSKY01 Data Plotting Script
Usage: python plot_logs.py <csv_file1> [csv_file2] [csv_file3] ...
       python plot_logs.py sensor_logs/*.csv
"""

import sys
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from datetime import datetime
import numpy as np
import os
from pathlib import Path
import math

def calculate_dew_point(temp_c, humidity_percent):
    """Calculate dew point using Magnus formula"""
    if pd.isna(temp_c) or pd.isna(humidity_percent):
        return np.nan
    
    a = 17.27
    b = 237.7
    
    alpha = ((a * temp_c) / (b + temp_c)) + math.log(humidity_percent / 100.0)
    dew_point = (b * alpha) / (a - alpha)
    
    return dew_point

def calculate_improved_light(adc_value, gain, exposure_time):
    """Calculate improved light measurement: ADC / gain × exposure_time"""
    if pd.isna(adc_value) or pd.isna(gain) or pd.isna(exposure_time) or gain == 0:
        return np.nan
    return adc_value / (gain * exposure_time)

def plot_amsky_data(csv_files):
    """Plot AMSKY01 sensor data in 3 subplots for multiple files"""
    
    dfs = []
    for csv_file in csv_files:
        try:
            df = pd.read_csv(csv_file, sep=',', header=0)
            df['timestamp'] = pd.to_datetime(df['timestamp_utc'])
            df['source_file'] = Path(csv_file).stem
            
            # Calculate dew point
            df['dew_point'] = df.apply(lambda row: calculate_dew_point(row['hygro_temp'], row['hygro_humid']), axis=1)
            
            # Calculate improved light measurements
            df['light_improved'] = df.apply(lambda row: calculate_improved_light(row['light_raw'], row['light_gain'], row['light_integration']), axis=1)
            df['light_ir_improved'] = df.apply(lambda row: calculate_improved_light(row['light_ir'], row['light_gain'], row['light_integration']), axis=1)
            
            dfs.append(df)
            print(f"Loaded {len(df)} data points from {csv_file}")
        except Exception as e:
            print(f"Error reading file {csv_file}: {e}")
            continue
    
    if not dfs:
        print("No valid data loaded.")
        return
    
    df = pd.concat(dfs).sort_values('timestamp').reset_index(drop=True)
    
    # Create figure with 4 subplots
    fig, (ax1, ax2, ax3, ax4) = plt.subplots(4, 1, figsize=(12, 12), sharex=True)
    if len(csv_files) == 1:
        fig.suptitle(f'AMSKY01 Sensor Data - {Path(csv_files[0]).name}', fontsize=14)
    else:
        fig.suptitle(f'AMSKY01 Sensor Data - {len(csv_files)} files combined', fontsize=14)
    
    # Plot 1: Temperature, Humidity and Dew Point
    ax1_twin = ax1.twinx()
    
    # Temperature and Dew Point on left axis
    line1 = ax1.plot(df['timestamp'], df['hygro_temp'], 'r-', linewidth=2, label='Temperature (°C)')
    line3 = ax1.plot(df['timestamp'], df['dew_point'], 'green', linewidth=2, alpha=0.7, label='Dew Point (°C)')
    ax1.set_ylabel('Temperature (°C)', color='red')
    ax1.tick_params(axis='y', labelcolor='red')
    ax1.grid(True, alpha=0.3)
    
    # Humidity on right axis
    line2 = ax1_twin.plot(df['timestamp'], df['hygro_humid'], 'b-', linewidth=2, label='Humidity (%)')
    ax1_twin.set_ylabel('Humidity (%)', color='blue')
    ax1_twin.tick_params(axis='y', labelcolor='blue')
    
    ax1.set_title('Temperature, Humidity and Dew Point')
    
    # Combined legend
    lines = line1 + line3 + line2
    labels = [l.get_label() for l in lines]
    ax1.legend(lines, labels, loc='upper left')
    
    # Plot 2: Light Sensor Data
    ax2_twin = ax2.twinx()

    # Lux and Improved Light on left axis
    #line1 = ax2.semilogy(df['timestamp'], df['light_lux_calc'], 'orange', linewidth=2, label='Light (lux)', marker='o', markersize=2)
    line2 = ax2.semilogy(df['timestamp'], df['light_improved'], 'brown', linewidth=2, label='Improved Light (lux)', alpha=0.7)

    # Raw and IR on right axis
    line3 = ax2_twin.semilogy(df['timestamp'], df['light_raw'], 'grey', linewidth=1, label='Light Raw', alpha=0.7, linestyle='dotted')
    line4 = ax2_twin.semilogy(df['timestamp'], df['light_ir'], 'black', linewidth=1, label='Light IR', alpha=0.7, linestyle='dotted')

    ax2.set_ylabel('Light (lux)', color='orange')
    ax2.tick_params(axis='y', labelcolor='orange')
    ax2.set_title('Light Sensor Data')
    ax2.grid(True, alpha=0.3)

    ax2_twin.set_ylabel('Raw Values', color='grey')
    ax2_twin.tick_params(axis='y', labelcolor='grey')

    # Combined legend
    lines = line2 + line3 + line4
    labels = [l.get_label() for l in lines]
    ax2.legend(lines, labels, loc='upper left')
    
    # Plot 3: Thermal Sensor Data
    thermal_columns = ['thermal_tl', 'thermal_tr', 'thermal_bl', 'thermal_br', 'thermal_center']
    thermal_labels = ['Thermal TL', 'Thermal TR', 'Thermal BL', 'Thermal BR', 'Thermal Center']
    colors = ['red', 'green', 'blue', 'magenta', 'black']
    linewidths = [1, 1, 1, 1, 2]
    
    for col, label, color, lw in zip(thermal_columns, thermal_labels, colors, linewidths):
        if col in df.columns:
            ax3.plot(df['timestamp'], df[col], color=color, linewidth=lw, label=label, alpha=0.8)
    
    ax3.set_ylabel('Temperature (K × 100)')
    ax3.set_title('Cloud Sensor Data')
    ax3.grid(True, alpha=0.3)
    ax3.legend()
    
    # Plot 4: Sensor Parameters (Gain and Integration Time)
    ax4_twin = ax4.twinx()
    
    # Gain on left axis
    line1 = ax4.plot(df['timestamp'], df['light_gain'], 'purple', linewidth=2, label='Gain', marker='s', markersize=3)
    ax4.set_ylabel('Gain', color='purple')
    ax4.tick_params(axis='y', labelcolor='purple')
    ax4.grid(True, alpha=0.3)
    
    # Integration time on right axis
    line2 = ax4_twin.plot(df['timestamp'], df['light_integration'], 'orange', linewidth=2, label='Integration Time (ms)', marker='o', markersize=3)
    ax4_twin.set_ylabel('Integration Time (ms)', color='orange')
    ax4_twin.tick_params(axis='y', labelcolor='orange')
    
    ax4.set_xlabel('Time')
    ax4.set_title('Light Sensor Parameters')
    
    # Combined legend
    lines = line1 + line2
    labels = [l.get_label() for l in lines]
    ax4.legend(lines, labels, loc='upper left')
    
    # Format x-axis for time with dynamic interval calculation
    time_range = df['timestamp'].max() - df['timestamp'].min()
    time_range_hours = time_range.total_seconds() / 3600
    
    # Calculate optimal interval based on total time range to aim for ~10-15 labels
    if time_range_hours < 1:
        # Less than 1 hour: use minute intervals
        optimal_interval_minutes = max(1, int(time_range_hours * 60 / 10))
        locator = mdates.MinuteLocator(interval=optimal_interval_minutes)
        formatter = mdates.DateFormatter('%H:%M:%S')
    elif time_range_hours < 6:
        # 1-6 hours: use 15-30 minute intervals
        optimal_interval_minutes = max(15, int(time_range_hours * 60 / 12))
        locator = mdates.MinuteLocator(interval=optimal_interval_minutes)
        formatter = mdates.DateFormatter('%H:%M')
    else:
        # More than 6 hours: use hour intervals
        optimal_interval_hours = max(1, int(time_range_hours / 10))
        locator = mdates.HourLocator(interval=optimal_interval_hours)
        formatter = mdates.DateFormatter('%H:%M')
    
    for ax in [ax1, ax2, ax3, ax4]:
        ax.xaxis.set_major_formatter(formatter)
        ax.xaxis.set_major_locator(locator)
    
    plt.xticks(rotation=45)
    plt.tight_layout()
    
    # Save plot
    if len(csv_files) == 1:
        output_file = csv_files[0].replace('.csv', '_plots.png')
    else:
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        output_file = f'amsky01_combined_{timestamp}_plots.png'
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved as: {output_file}")
    
    # Show statistics
    print("\\nCombined Data Statistics:")
    print(f"Total data points: {len(df)}")
    print(f"Time range: {df['timestamp'].min()} to {df['timestamp'].max()}")
    
    # Temperature stats (skip NaN values)
    temp_data = df['hygro_temp'].dropna()
    if not temp_data.empty:
        print(f"Temperature: {temp_data.min():.1f}°C to {temp_data.max():.1f}°C")
    
    # Humidity stats (skip NaN values)
    humid_data = df['hygro_humid'].dropna()
    if not humid_data.empty:
        print(f"Humidity: {humid_data.min():.1f}% to {humid_data.max():.1f}%")
    
    # Dew Point stats (skip NaN values)
    dew_data = df['dew_point'].dropna()
    if not dew_data.empty:
        print(f"Dew Point: {dew_data.min():.1f}°C to {dew_data.max():.1f}°C")
    
    # Light stats (skip NaN values)
    light_data = df['light_lux_calc'].dropna()
    if not light_data.empty:
        print(f"Light (calculated): {light_data.min():.6f} to {light_data.max():.6f} lux")
    
    # Improved Light stats (skip NaN values)
    light_improved_data = df['light_improved'].dropna()
    if not light_improved_data.empty:
        print(f"Light (improved): {light_improved_data.min():.6f} to {light_improved_data.max():.6f} units")

def main():
    if len(sys.argv) < 2:
        print("Usage: python plot_logs.py <csv_file1> [csv_file2] [csv_file3] ...")
        sys.exit(1)
    
    csv_files = sys.argv[1:]
    plot_amsky_data(csv_files)

if __name__ == "__main__":
    main()
