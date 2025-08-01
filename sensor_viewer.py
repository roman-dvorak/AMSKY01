#!/usr/bin/env python3
"""
AMSKY01 Sensor Data Viewer
Real-time visualization of sensor data with CLI interface
"""

import argparse
import serial
import threading
import time
import sys
from datetime import datetime, timezone, timedelta
import json
import math
import csv
import os


# CLI imports (optional)
try:
    import curses
    CLI_AVAILABLE = True
except ImportError:
    CLI_AVAILABLE = False
    print("Warning: curses not available.")


class SensorData:
    """Container for sensor data with thread-safe access"""
    def __init__(self):
        self.lock = threading.Lock()
        
        # Latest values for display
        self.latest = {
            'hygro': {'temp': None, 'humid': None},
            'light': {'lux': None, 'raw': None, 'ir': None, 'gain': None, 'integration': None},
            'thermal': {'tl': None, 'tr': None, 'bl': None, 'br': None, 'center': None}
        }
        
    def add_data(self, sensor_type, data):
        """Add new sensor data point"""
        with self.lock:
            print(f"[DEBUG SensorData] Adding {sensor_type} data: {data}")
            
            if sensor_type == 'hygro' and len(data) >= 2:
                try:
                    temp = float(data[0])
                    humid = float(data[1])
                    self.latest['hygro'] = {'temp': temp, 'humid': humid}
                except ValueError:
                    pass
                    
            elif sensor_type == 'light' and len(data) >= 5:
                try:
                    lux = float(data[0])
                    raw = int(data[1])
                    ir = int(data[2])
                    gain = data[3]
                    integration = data[4]

                    # Calculate true lux using gain and integration
                    calculated_lux = self.calculate_true_lux(raw, gain, integration)
                    self.latest['light'] = {
                        'lux': calculated_lux, 'raw': raw, 'ir': ir, 
                        'gain': gain, 'integration': integration
                    }
                except ValueError:
                    pass
                    
            elif sensor_type == 'thermal' and len(data) >= 5:
                try:
                    tl = float(data[0])
                    tr = float(data[1])
                    bl = float(data[2])
                    br = float(data[3])
                    center = float(data[4])
                    
                    self.latest['thermal'] = {
                        'tl': tl, 'tr': tr, 'bl': bl, 'br': br, 'center': center
                    }
                except ValueError:
                    pass
                    
    def get_latest_data(self):
        """Get latest sensor values"""
        with self.lock:
            return self.latest.copy()
            
    def calculate_true_lux(self, raw, gain, integration):
        """Calculate true lux value based on gain and integration time"""
        try:
            # Safe conversion of integration time
            integration_time = float(integration) if integration != '0' else 100.0
            if integration_time == 0.0:
                integration_time = 100.0  # Default fallback
            
            integration_scale = 100.0 / integration_time
            
            # Handle different gain values based on TSL2591 settings
            gain_map = {
                '1': 1.0,      # 1x gain
                '25': 25.0,    # 25x gain  
                '428': 428.0,  # 428x gain
                '9876': 9876.0 # Max gain
            }
            gain_multiplier = gain_map.get(str(gain), 1.0)
            
            # Calculate lux with proper scaling
            # TSL2591 coefficient (approximate)
            lux = (raw * integration_scale) / gain_multiplier * 0.408
            
            return self.format_lux_value(lux)
            
        except (ValueError, ZeroDivisionError) as e:
            return "Error"
    
    def format_lux_value(self, lux):
        """Format lux value with appropriate units"""
        if lux >= 1e6:
            return f"{lux / 1e6:.3f} Mlux"
        elif lux >= 1e3:
            return f"{lux / 1e3:.3f} klux"
        elif lux >= 1.0:
            return f"{lux:.3f} lux"
        elif lux >= 1e-3:
            return f"{lux * 1e3:.3f} mlux"
        elif lux >= 1e-6:
            return f"{lux * 1e6:.3f} μlux"
        else:
            return f"{lux * 1e9:.3f} nlux"


class DataLogger:
    """CSV data logger with automatic file rotation every 10 minutes"""
    def __init__(self, sensor_data, log_dir="sensor_logs"):
        self.sensor_data = sensor_data
        self.log_dir = log_dir
        self.current_file = None
        self.current_writer = None
        self.current_file_handle = None
        self.data_buffer = []
        self.last_save_time = time.time()
        self.file_start_time = None
        self.next_rotation_time = None
        self.lock = threading.Lock()
        self.running = False
        self.logger_thread = None
        
        # Create log directory if it doesn't exist
        os.makedirs(self.log_dir, exist_ok=True)
        
        # CSV headers
        self.csv_headers = [
            'timestamp_utc', 'unix_timestamp',
            'hygro_temp', 'hygro_humid',
            'light_lux_calc', 'light_raw', 'light_ir', 'light_gain', 'light_integration',
            'thermal_tl', 'thermal_tr', 'thermal_bl', 'thermal_br', 'thermal_center'
        ]
        
    def start(self):
        """Start the data logger"""
        self.running = True
        self._create_new_file()
        
        # Calculate next 10-minute boundary for file rotation
        self._calculate_next_rotation_time()
        
        self.logger_thread = threading.Thread(target=self._logger_loop, daemon=True)
        self.logger_thread.start()
        print(f"Data logger started - logging to {self.log_dir}/")
        
    def stop(self):
        """Stop the data logger and save remaining data"""
        self.running = False
        if self.logger_thread:
            self.logger_thread.join(timeout=2.0)
        self._save_buffered_data(force=True)
        self._close_current_file()
        print("Data logger stopped")
        
    def log_data_point(self, sensor_type, data):
        """Log a single data point to buffer"""
        with self.lock:
            timestamp_utc = datetime.now(timezone.utc)
            unix_timestamp = timestamp_utc.timestamp()
            
            # Find or create entry for this timestamp (rounded to nearest second)
            timestamp_key = int(unix_timestamp)
            
            # Find existing entry or create new one
            entry = None
            for item in self.data_buffer:
                if int(item['unix_timestamp']) == timestamp_key:
                    entry = item
                    break
                    
            if entry is None:
                entry = {
                    'timestamp_utc': timestamp_utc.isoformat(),
                    'unix_timestamp': unix_timestamp,
                    'hygro_temp': None, 'hygro_humid': None,
                    'light_lux_calc': None, 'light_raw': None, 'light_ir': None, 
                    'light_gain': None, 'light_integration': None,
                    'thermal_tl': None, 'thermal_tr': None, 'thermal_bl': None, 
                    'thermal_br': None, 'thermal_center': None
                }
                self.data_buffer.append(entry)
                
            # Update entry with new sensor data
            try:
                if sensor_type == 'hygro' and len(data) >= 2:
                    entry['hygro_temp'] = float(data[0])
                    entry['hygro_humid'] = float(data[1])
                    
                elif sensor_type == 'light' and len(data) >= 5:
                    # For light sensor, we need to calculate lux properly
                    raw = int(data[1])
                    gain = data[3]
                    integration = data[4]
                    
                    # Calculate numerical lux value (not formatted string)
                    calculated_lux = self._calculate_numerical_lux(raw, gain, integration)
                    
                    entry['light_lux_calc'] = calculated_lux
                    entry['light_raw'] = raw
                    entry['light_ir'] = int(data[2])
                    entry['light_gain'] = gain
                    entry['light_integration'] = integration
                    
                elif sensor_type == 'thermal' and len(data) >= 5:
                    entry['thermal_tl'] = float(data[0])
                    entry['thermal_tr'] = float(data[1])
                    entry['thermal_bl'] = float(data[2])
                    entry['thermal_br'] = float(data[3])
                    entry['thermal_center'] = float(data[4])
                    
            except (ValueError, IndexError) as e:
                print(f"[DEBUG DataLogger] Error processing {sensor_type} data: {e}")
                
    def _calculate_numerical_lux(self, raw, gain, integration):
        """Calculate numerical lux value (not formatted string)"""
        try:
            integration_time = float(integration) if integration != '0' else 100.0
            if integration_time == 0.0:
                integration_time = 100.0
            
            integration_scale = 100.0 / integration_time
            
            gain_map = {
                '1': 1.0, '25': 25.0, '428': 428.0, '9876': 9876.0
            }
            gain_multiplier = gain_map.get(str(gain), 1.0)
            
            lux = (raw * integration_scale) / gain_multiplier * 0.408
            return lux
            
        except (ValueError, ZeroDivisionError):
            return None
            
    def _logger_loop(self):
        """Main logger loop - checks for data to save every 5 seconds"""
        while self.running:
            current_time = time.time()
            
            # Check if current time passed the next_rotation_time (UTC aligned)
            if self.next_rotation_time and current_time >= self.next_rotation_time:
                self._save_buffered_data(force=True)
                self._close_current_file()
                self._create_new_file()
                self._calculate_next_rotation_time()
                
            # Save data every 30 seconds or if buffer gets large
            elif (current_time - self.last_save_time >= 30 or 
                  len(self.data_buffer) >= 100):
                self._save_buffered_data()
                
            time.sleep(5)  # Check every 5 seconds
            
    def _calculate_next_rotation_time(self):
        """Calculate next rotation time aligned to next 10-minute UTC boundary"""
        now = datetime.now(timezone.utc)
        
        # Calculate next 10-minute boundary
        current_minute = now.minute
        next_ten_minute = ((current_minute // 10) + 1) * 10
        
        if next_ten_minute >= 60:
            # Next boundary is in the next hour
            next_boundary = now.replace(minute=0, second=0, microsecond=0) + timedelta(hours=1)
        else:
            # Next boundary is in this hour
            next_boundary = now.replace(minute=next_ten_minute, second=0, microsecond=0)
        
        self.next_rotation_time = next_boundary.timestamp()
        
        print(f"[DataLogger] Next file rotation at: {next_boundary.strftime('%Y-%m-%d %H:%M:%S')} UTC")
            
    def _create_new_file(self):
        """Create a new CSV file with timestamp"""
        timestamp = datetime.now(timezone.utc)
        filename = f"amsky01_data_{timestamp.strftime('%Y%m%d_%H%M%S')}_UTC.csv"
        filepath = os.path.join(self.log_dir, filename)
        
        try:
            self.current_file_handle = open(filepath, 'w', newline='', encoding='utf-8')
            self.current_writer = csv.DictWriter(self.current_file_handle, fieldnames=self.csv_headers)
            self.current_writer.writeheader()
            self.current_file_handle.flush()
            
            self.current_file = filepath
            self.file_start_time = time.time()
            print(f"[DataLogger] Created new log file: {filename}")
            
        except Exception as e:
            print(f"[DataLogger] Error creating file {filepath}: {e}")
            self.current_file_handle = None
            self.current_writer = None
            
    def _close_current_file(self):
        """Close current CSV file"""
        if self.current_file_handle:
            try:
                self.current_file_handle.close()
                print(f"[DataLogger] Closed log file: {os.path.basename(self.current_file)}")
            except Exception as e:
                print(f"[DataLogger] Error closing file: {e}")
            finally:
                self.current_file_handle = None
                self.current_writer = None
                self.current_file = None
                
    def _save_buffered_data(self, force=False):
        """Save buffered data to CSV file"""
        with self.lock:
            if not self.data_buffer or not self.current_writer:
                return
                
            try:
                # Sort buffer by timestamp before writing
                self.data_buffer.sort(key=lambda x: x['unix_timestamp'])
                
                # Write all buffered data
                for entry in self.data_buffer:
                    self.current_writer.writerow(entry)
                    
                self.current_file_handle.flush()
                
                entries_written = len(self.data_buffer)
                self.data_buffer.clear()
                self.last_save_time = time.time()
                
                if entries_written > 0:
                    print(f"[DataLogger] Saved {entries_written} entries to {os.path.basename(self.current_file)}")
                    
            except Exception as e:
                print(f"[DataLogger] Error saving data: {e}")


class SerialReader:
    """Serial port reader in separate thread"""
    def __init__(self, port, baudrate, sensor_data, data_logger=None):
        self.port = port
        self.baudrate = baudrate
        self.sensor_data = sensor_data
        self.data_logger = data_logger
        self.running = False
        self.thread = None
        self.serial_conn = None
        
    def start(self):
        """Start reading from serial port with robust connection"""
        try:
            # Close any existing connection
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.close()
                time.sleep(0.5)
            
            # Create new serial connection with more robust settings
            self.serial_conn = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=1.0,          # Read timeout
                write_timeout=1.0,    # Write timeout
                xonxoff=False,        # Software flow control
                rtscts=False,         # Hardware flow control
                dsrdtr=False          # Hardware flow control
            )
            
            # Clear any existing data in buffers
            time.sleep(0.1)
            self.serial_conn.reset_input_buffer()
            self.serial_conn.reset_output_buffer()
            
            # Verify connection
            if not self.serial_conn.is_open:
                raise serial.SerialException("Failed to open serial port")
                
            print(f"Serial port {self.port} opened successfully")
            
            self.running = True
            self.thread = threading.Thread(target=self._read_loop, daemon=True)
            self.thread.start()
            return True
            
        except Exception as e:
            print(f"Error opening serial port: {e}")
            if self.serial_conn:
                try:
                    self.serial_conn.close()
                except:
                    pass
                self.serial_conn = None
            return False
            
    def stop(self):
        """Stop reading from serial port"""
        self.running = False
        if self.serial_conn:
            self.serial_conn.close()
            
    def _read_loop(self):
        """Main reading loop with improved error handling"""
        consecutive_errors = 0
        last_data_time = time.time()
        data_count = 0
        buffer = ""
        reconnect_attempts = 0
        max_reconnect_attempts = 5
        
        while self.running:
            try:
                if self.serial_conn and self.serial_conn.is_open:
                    # Check if data is available
                    bytes_waiting = self.serial_conn.in_waiting
                    if bytes_waiting > 0:
                        try:
                            # Read available data with timeout protection
                            chunk_size = min(bytes_waiting, 1024)
                            chunk_bytes = self.serial_conn.read(chunk_size)
                            
                            # Handle empty read (device disconnected or busy)
                            if not chunk_bytes:
                                consecutive_errors += 1
                                print(f"Empty read despite {bytes_waiting} bytes waiting - potential connection issue")
                                if consecutive_errors >= 3:
                                    print("Attempting to reconnect...")
                                    self._attempt_reconnect()
                                    consecutive_errors = 0
                                    reconnect_attempts += 1
                                    if reconnect_attempts >= max_reconnect_attempts:
                                        print(f"Max reconnect attempts ({max_reconnect_attempts}) reached, stopping")
                                        self.running = False
                                        break
                                time.sleep(0.1)
                                continue
                            
                            # Decode data
                            chunk = chunk_bytes.decode('utf-8', errors='ignore')
                            buffer += chunk
                            
                            # Process complete lines
                            while '\n' in buffer:
                                line, buffer = buffer.split('\n', 1)
                                line = line.strip()
                                
                                if line and ',' in line:
                                    parts = line.split(',')
                                    # Handle $ prefix in sensor type
                                    sensor_type_raw = parts[0]
                                    if sensor_type_raw.startswith('$'):
                                        sensor_type_raw = sensor_type_raw[1:]  # Remove $ prefix
                                    
                                    # Map cloud to thermal for compatibility
                                    if sensor_type_raw == 'cloud':
                                        sensor_type_raw = 'thermal'
                                    
                                    if len(parts) >= 2 and sensor_type_raw in ['hygro', 'light', 'thermal']:
                                        sensor_type = sensor_type_raw
                                        data = parts[1:]
                                        self.sensor_data.add_data(sensor_type, data)
                                        
                                        # Log to CSV if logger is available
                                        if self.data_logger:
                                            self.data_logger.log_data_point(sensor_type, data)
                                        
                                        consecutive_errors = 0  # Reset error counter on success
                                        reconnect_attempts = 0  # Reset reconnect counter on success
                                        data_count += 1
                                        last_data_time = time.time()
                                        print(f"[DEBUG SerialReader] [{data_count:04d}] {sensor_type}: {','.join(data)}")
                                    else:
                                        print(f"Invalid sensor type or format: {line} (sensor_type: {sensor_type_raw})")
                                elif line and len(line) > 3:
                                    print(f"Invalid data format: {line}")
                                    
                        except serial.SerialTimeoutException:
                            # Timeout is normal, just continue
                            pass
                        except Exception as read_error:
                            consecutive_errors += 1
                            print(f"Read operation error: {read_error}")
                            
                    else:
                        # No data available - check for timeout
                        if time.time() - last_data_time > 10.0:
                            print(f"No data received for 10 seconds, checking connection...")
                            last_data_time = time.time()
                            # Try to flush input buffer
                            try:
                                self.serial_conn.reset_input_buffer()
                            except:
                                pass
                    
                    time.sleep(0.05)  # Reasonable sleep to prevent CPU spinning
                else:
                    print("Serial connection closed")
                    break
                    
            except serial.SerialException as e:
                consecutive_errors += 1
                if self.running:
                    print(f"Serial error #{consecutive_errors}: {e}")
                    if consecutive_errors >= 3:
                        print("Attempting to reconnect due to serial errors...")
                        self._attempt_reconnect()
                        consecutive_errors = 0
                        reconnect_attempts += 1
                        if reconnect_attempts >= max_reconnect_attempts:
                            print(f"Max reconnect attempts ({max_reconnect_attempts}) reached, stopping reader")
                            self.running = False
                            break
                    time.sleep(0.5)
            except UnicodeDecodeError as e:
                print(f"Unicode decode error: {e} - clearing buffer and continuing...")
                buffer = ""  # Clear corrupted buffer
            except Exception as e:
                consecutive_errors += 1
                if self.running:
                    print(f"Unexpected error #{consecutive_errors}: {e}")
                    if consecutive_errors >= 5:
                        print(f"Too many unexpected errors ({consecutive_errors}), stopping reader")
                        self.running = False
                        break
                    time.sleep(0.1)
                    
    def _attempt_reconnect(self):
        """Attempt to reconnect to serial port"""
        try:
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.close()
            time.sleep(1)  # Wait before reconnecting
            
            self.serial_conn = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"Successfully reconnected to {self.port}")
            return True
        except Exception as e:
            print(f"Reconnection failed: {e}")
            return False


class CLIInterface:
    """ncurses-based CLI interface"""
    def __init__(self, sensor_data, serial_reader):
        self.sensor_data = sensor_data
        self.serial_reader = serial_reader
        self.stdscr = None
        
    def run(self):
        """Run the CLI interface"""
        curses.wrapper(self._main_loop)
        
    def _main_loop(self, stdscr):
        """Main curses loop"""
        self.stdscr = stdscr
        curses.curs_set(0)  # Hide cursor
        stdscr.nodelay(1)   # Non-blocking input
        stdscr.timeout(100) # Refresh every 100ms
        
        # Colors
        curses.start_color()
        curses.init_pair(1, curses.COLOR_GREEN, curses.COLOR_BLACK)
        curses.init_pair(2, curses.COLOR_YELLOW, curses.COLOR_BLACK)
        curses.init_pair(3, curses.COLOR_RED, curses.COLOR_BLACK)
        curses.init_pair(4, curses.COLOR_CYAN, curses.COLOR_BLACK)
        
        while True:
            try:
                self._draw_screen()
                
                # Check for quit
                key = stdscr.getch()
                if key == ord('q') or key == ord('Q'):
                    break
                    
            except KeyboardInterrupt:
                break
                
    def _draw_screen(self):
        """Draw the main screen"""
        self.stdscr.clear()
        height, width = self.stdscr.getmaxyx()
        
        # Title
        title = "AMSKY01 Sensor Data Viewer (CLI Mode)"
        self.stdscr.addstr(0, (width - len(title)) // 2, title, curses.color_pair(4) | curses.A_BOLD)
        
        # Instructions
        self.stdscr.addstr(1, 0, "Press 'q' to quit", curses.color_pair(2))
        
        # Get latest data
        data = self.sensor_data.get_latest_data()
        
        # Hygro section
        self.stdscr.addstr(3, 0, "┌─ HYGRO SENSOR ─────────────────────┐", curses.color_pair(1))
        if data['hygro']['temp'] is not None:
            self.stdscr.addstr(4, 0, f"│ Temperature: {data['hygro']['temp']:7.2f} °C       │")
            self.stdscr.addstr(5, 0, f"│ Humidity:    {data['hygro']['humid']:7.2f} %        │")
        else:
            self.stdscr.addstr(4, 0, "│ Temperature: ---.-- °C       │")
            self.stdscr.addstr(5, 0, "│ Humidity:    ---.-- %        │")
        self.stdscr.addstr(6, 0, "└────────────────────────────────────┘")
        
        # Light section
        self.stdscr.addstr(8, 0, "┌─ LIGHT SENSOR ─────────────────────┐", curses.color_pair(1))
        if data['light']['lux'] is not None:
            self.stdscr.addstr(9, 0,  f"│ Lux:         {str(data['light']['lux']):>15s} │")
            self.stdscr.addstr(10, 0, f"│ Raw:         {data['light']['raw']:10d}      │")
            self.stdscr.addstr(11, 0, f"│ IR:          {data['light']['ir']:10d}      │")
            self.stdscr.addstr(12, 0, f"│ Gain:        {str(data['light']['gain']):>10s}      │")
            self.stdscr.addstr(13, 0, f"│ Integration: {str(data['light']['integration']):>10s} ms  │")
        else:
            self.stdscr.addstr(9, 0,  "│ Lux:         ----------      │")
            self.stdscr.addstr(10, 0, "│ Raw:         ----------      │")
            self.stdscr.addstr(11, 0, "│ IR:          ----------      │")
            self.stdscr.addstr(12, 0, "│ Gain:        ----------      │")
            self.stdscr.addstr(13, 0, "│ Integration: ---------- ms  │")
        self.stdscr.addstr(14, 0, "└────────────────────────────────────┘")
        
        # Thermal section
        self.stdscr.addstr(16, 0, "┌─ THERMAL SENSOR ───────────────────┐", curses.color_pair(1))
        if data['thermal']['tl'] is not None:
            self.stdscr.addstr(17, 0, f"│ Top-Left:    {data['thermal']['tl']:8.2f}         │")
            self.stdscr.addstr(18, 0, f"│ Top-Right:   {data['thermal']['tr']:8.2f}         │")
            self.stdscr.addstr(19, 0, f"│ Bottom-Left: {data['thermal']['bl']:8.2f}         │")
            self.stdscr.addstr(20, 0, f"│ Bottom-Right:{data['thermal']['br']:8.2f}         │")
            self.stdscr.addstr(21, 0, f"│ Center:      {data['thermal']['center']:8.2f}         │")
        else:
            self.stdscr.addstr(17, 0, "│ Top-Left:    --------         │")
            self.stdscr.addstr(18, 0, "│ Top-Right:   --------         │")
            self.stdscr.addstr(19, 0, "│ Bottom-Left: --------         │")
            self.stdscr.addstr(20, 0, "│ Bottom-Right:--------         │")
            self.stdscr.addstr(21, 0, "│ Center:      --------         │")
        self.stdscr.addstr(22, 0, "└────────────────────────────────────┘")
        
        # Connection status
        status_color = curses.color_pair(1) if self.serial_reader.running else curses.color_pair(3)
        status_text = "Connected" if self.serial_reader.running else "Disconnected"
        self.stdscr.addstr(24, 0, f"Status: {status_text}", status_color)
        self.stdscr.addstr(25, 0, f"Port: {self.serial_reader.port}")
        
        self.stdscr.refresh()




def list_serial_ports():
    """List all available serial ports"""
    try:
        import serial.tools.list_ports as list_ports
        ports = list_ports.comports()
        available_ports = []
        
        print("\nAvailable serial ports:")
        for port in ports:
            print(f"  {port.device} - {port.description}")
            available_ports.append(port.device)
        
        if not available_ports:
            print("  No serial ports found")
        
        return available_ports
    except ImportError:
        print("Error: serial.tools.list_ports not available")
        return []


def main():
    """Main function"""
    parser = argparse.ArgumentParser(description='AMSKY01 Sensor Data Viewer')
    parser.add_argument('--port', '-p', default='/dev/ttyACM0', 
                        help='Serial port (default: /dev/ttyACM0)')
    parser.add_argument('--baudrate', '-b', type=int, default=115200,
                        help='Baudrate (default: 115200)')
    parser.add_argument('--log', action='store_true',
                        help='Enable CSV data logging to sensor_logs/ directory')
    parser.add_argument('--list-ports', '-l', action='store_true',
                        help='List available serial ports and exit')
    
    args = parser.parse_args()
    
    # List ports if requested
    if args.list_ports:
        list_serial_ports()
        sys.exit(0)
    
    # Check CLI availability
    if not CLI_AVAILABLE:
        print("Error: curses not available.")
        sys.exit(1)
    
    # List available ports for information
    available_ports = list_serial_ports()
    if args.port not in available_ports and available_ports:
        print(f"\nWarning: Selected port {args.port} not found in available ports.")
        print("Continuing anyway - device might not be connected yet.")
    
    # Create data container
    sensor_data = SensorData()
    
    # Create data logger if requested
    data_logger = None
    if args.log:
        data_logger = DataLogger(sensor_data)
        data_logger.start()
    
    # Create serial reader
    serial_reader = SerialReader(args.port, args.baudrate, sensor_data, data_logger)
    
    # Start serial reading and data logging
    if not serial_reader.start():
        print(f"Failed to open serial port {args.port}")
        print("Try using --list-ports to see available ports")
        sys.exit(1)
    
    print(f"Connected to {args.port} at {args.baudrate} baud")
    print("Starting CLI interface...")
    
    try:
        app = CLIInterface(sensor_data, serial_reader)
        app.run()
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        # Stop serial reader
        serial_reader.stop()
        
        # Stop data logger and save final data (if enabled)
        if data_logger:
            data_logger.stop()
        
        print("Goodbye!")


if __name__ == '__main__':
    main()
