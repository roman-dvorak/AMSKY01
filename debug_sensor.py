#!/usr/bin/env python3
"""
Debug script for AMSKY01 sensor data reception
"""
import serial
import time
import sys
from collections import deque

def test_serial_connection(port, baudrate=115200, duration=10):
    """Test serial connection and print raw data"""
    print(f"Testing serial connection on {port} at {baudrate} baud...")
    print(f"Will collect data for {duration} seconds...")
    print("=" * 50)
    
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"✓ Successfully opened {port}")
        
        start_time = time.time()
        hygro_count = 0
        light_count = 0
        thermal_count = 0
        total_lines = 0
        
        while time.time() - start_time < duration:
            if ser.in_waiting:
                try:
                    line = ser.readline().decode('utf-8').strip()
                    if line:
                        total_lines += 1
                        print(f"Raw: {line}")
                        
                        # Parse the line
                        if ',' in line:
                            parts = line.split(',')
                            sensor_type = parts[0]
                            
                            if sensor_type == 'hygro' and len(parts) >= 3:
                                hygro_count += 1
                                temp = float(parts[1])
                                humid = float(parts[2])
                                print(f"  → HYGRO: temp={temp:.2f}°C, humidity={humid:.2f}%")
                                
                            elif sensor_type == 'light' and len(parts) >= 6:
                                light_count += 1
                                lux = float(parts[1])
                                raw = int(parts[2])
                                ir = int(parts[3])
                                gain = parts[4]
                                integration = parts[5]
                                print(f"  → LIGHT: lux={lux:.6f}, raw={raw}, ir={ir}, gain={gain}, int={integration}")
                                
                            elif sensor_type == 'thermal' and len(parts) >= 6:
                                thermal_count += 1
                                tl = float(parts[1])
                                tr = float(parts[2])
                                bl = float(parts[3])
                                br = float(parts[4])
                                center = float(parts[5])
                                print(f"  → THERMAL: tl={tl:.2f}, tr={tr:.2f}, bl={bl:.2f}, br={br:.2f}, center={center:.2f}")
                                
                            else:
                                print(f"  → UNKNOWN: {sensor_type} with {len(parts)-1} data values")
                        
                except UnicodeDecodeError as e:
                    print(f"Decode error: {e}")
                except ValueError as e:
                    print(f"Parse error: {e}")
            
            time.sleep(0.01)
        
        ser.close()
        
        print("=" * 50)
        print(f"Summary after {duration} seconds:")
        print(f"Total lines received: {total_lines}")
        print(f"Hygro readings: {hygro_count}")
        print(f"Light readings: {light_count}")
        print(f"Thermal readings: {thermal_count}")
        
        if total_lines == 0:
            print("❌ No data received! Check:")
            print("  - Device is connected and powered")
            print("  - Correct serial port")
            print("  - Correct baudrate")
            print("  - Device is actually transmitting data")
        elif hygro_count == 0 and light_count == 0 and thermal_count == 0:
            print("❌ Data received but couldn't parse sensor readings! Check:")
            print("  - Data format matches expected format")
            print("  - No corruption in transmission")
        else:
            print("✓ Data reception working correctly!")
        
        return total_lines > 0
        
    except serial.SerialException as e:
        print(f"❌ Serial port error: {e}")
        return False
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
        return False

def simulate_data_collection():
    """Simulate the data collection like in the main program"""
    print("\nSimulating data collection similar to main program...")
    
    # Simulate some data points
    from sensor_viewer import SensorData
    
    sensor_data = SensorData(max_points=100)
    
    # Add some test data
    sensor_data.add_data('hygro', ['20.5', '65.2'])
    sensor_data.add_data('light', ['0.000123', '5432', '1234', '9876', '400'])
    sensor_data.add_data('thermal', ['65.1', '65.2', '64.9', '65.0', '64.8'])
    
    # Test data retrieval
    latest = sensor_data.get_latest_data()
    plot_data = sensor_data.get_plot_data()
    
    print("Latest data:", latest)
    print("Plot data lengths:")
    for key, value in plot_data.items():
        print(f"  {key}: {len(value)} points")
    
    if any(len(v) > 0 for v in plot_data.values()):
        print("✓ Data collection simulation working!")
    else:
        print("❌ Data collection simulation failed!")

def list_serial_ports():
    """List available serial ports"""
    print("Available serial ports:")
    try:
        import serial.tools.list_ports
        ports = serial.tools.list_ports.comports()
        for port in ports:
            print(f"  {port.device}: {port.description}")
        if not ports:
            print("  No serial ports found")
    except ImportError:
        print("  (Cannot list ports - pyserial tools not available)")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python debug_sensor.py <serial_port> [baudrate] [duration]")
        print("Example: python debug_sensor.py /dev/ttyACM0 115200 10")
        print()
        list_serial_ports()
        sys.exit(1)
    
    port = sys.argv[1]
    baudrate = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
    duration = int(sys.argv[3]) if len(sys.argv) > 3 else 10
    
    success = test_serial_connection(port, baudrate, duration)
    
    if success:
        simulate_data_collection()
