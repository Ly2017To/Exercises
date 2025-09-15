import socket
import os
import glob
import re
import signal
import sys
import time

class HexClient:
    def __init__(self, host='127.0.0.1', port=12345):
        self.host = host
        self.port = port
        self.running = False
        self.client_socket = None
    
    def signal_handler(self, sig, frame):
        """Handle keyboard interrupt signals"""
        print("\nReceived shutdown signal...")
        self.running = False
        self.shutdown()
    
    def shutdown(self):
        """Gracefully shutdown the client"""
        print("Shutting down client...")
        
        # Close client socket
        if self.client_socket:
            self.client_socket.close()
            print("Client socket closed")
        
        print("Client shutdown complete")
        sys.exit(0)
    
    def extract_hex_from_eventlog(self, line):
    
        hex_numbers = []
        
        # Extract the part after "eventLog-"
        hex_part = line.split("eventLog-")[1].strip()
            
        # Split into individual hex strings
        hex_strings = hex_part.split()
            
        for hex_str in hex_strings:
            # Clean and validate each hex string
            clean_hex = hex_str.strip().upper()
                
            # Check if it's a valid 2-character hex number
            if len(clean_hex) == 2 and all(c in '0123456789ABCDEF' for c in clean_hex):
                hex_numbers.append(clean_hex)
            else:
                print(f"Warning: Invalid hex format '{hex_str}' in line")
        
        return hex_numbers
    
    def create_32byte_chunk(self, hex_list):
        """Create a 32-byte chunk from hex list, pad with zeros if needed"""
        if len(hex_list) > 32:
            print(f"Warning: More than 32 hex numbers found, using first 32")
            hex_list = hex_list[:32]
        elif len(hex_list) < 32:
            print(f"Warning: Only {len(hex_list)} hex numbers found, padding with zeros")
            # Pad with zeros to make exactly 32 bytes
            hex_list.extend(['00'] * (32 - len(hex_list)))
        
        # Combine into a single hex string
        combined_hex = ''.join(hex_list)
        
        try:
            # Convert to bytes
            chunk_bytes = bytes.fromhex(combined_hex)
            return chunk_bytes
        except ValueError as e:
            print(f"Error creating bytes from hex: {e}")
            return None
    
    def process_file(self, file_path):
        """Read a file and process each line with eventLog- format"""
        line_chunks = []
        lines_processed = 0
        lines_with_eventlog = 0
        
        try:
            with open(file_path, 'r', encoding='utf-8') as file:
                for line_num, line in enumerate(file, 1):
                    if not self.running:
                        print("Client shutdown requested during file processing")
                        break
                        
                    line = line.strip()
                    if not line:  # Skip empty lines
                        continue
                    
                    lines_processed += 1
                    
                    # Check if this line contains eventLog- data
                    if "eventLog-" in line:
                        lines_with_eventlog += 1
                        
                        print(f"Processing line {line_num}: {line[:80]}..." if len(line) > 80 else f"Processing line {line_num}: {line}")
                        
                        # Extract hex numbers from eventLog- format
                        hex_numbers = self.extract_hex_from_eventlog(line)
                        
                        if hex_numbers:
                            print(f"  Found {len(hex_numbers)} hex numbers: {' '.join(hex_numbers[:8])}{'...' if len(hex_numbers) > 8 else ''}")
                            
                            # Create 32-byte chunk
                            chunk = self.create_32byte_chunk(hex_numbers)
                            
                            if chunk:
                                line_chunks.append(chunk)
                                print(f"  Created 32-byte chunk from line {line_num}")
                            else:
                                print(f"  Failed to create chunk from line {line_num}")
                        else:
                            print(f"  No valid hex numbers found in eventLog- at line {line_num}")
            
            print(f"\nFile processing summary for {file_path}:")
            print(f"  Total lines processed: {lines_processed}")
            print(f"  Lines with eventLog-: {lines_with_eventlog}")
            print(f"  32-byte chunks created: {len(line_chunks)}")
            
            return line_chunks
            
        except UnicodeDecodeError:
            # Try with different encoding if UTF-8 fails
            try:
                print("Trying with latin-1 encoding...")
                with open(file_path, 'r', encoding='latin-1') as file:
                    for line_num, line in enumerate(file, 1):
                        if not self.running:
                            break
                        line = line.strip()
                        if not line:
                            continue
                        
                        if "eventLog-" in line:
                            hex_numbers = self.extract_hex_from_eventlog(line)
                            if hex_numbers:
                                chunk = self.create_32byte_chunk(hex_numbers)
                                if chunk:
                                    line_chunks.append(chunk)
                return line_chunks
            except Exception as e:
                print(f"Error reading file {file_path} with latin-1 encoding: {e}")
                return []
            
        except Exception as e:
            print(f"Error reading file {file_path}: {e}")
            return []
    
    def send_hex_data(self, hex_data_list):
        """Send hex data to server"""
        if not hex_data_list:
            print("No valid hex data to send!")
            return False
        
        try:
            # Create TCP socket
            self.client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            
            # Connect to server
            self.client_socket.connect((self.host, self.port))
            print(f"Connected to server at {self.host}:{self.port}")
            print(f"Sending {len(hex_data_list)} 32-byte chunks...")
            print("Press Ctrl+C to cancel sending")
            print("-" * 60)
            
            # Send each 32-byte chunk
            success_count = 0
            for i, hex_bytes in enumerate(hex_data_list, 1):
                if not self.running:
                    print("Sending cancelled by user")
                    break
                    
                hex_string = hex_bytes.hex().upper()
                
                # Format hex string for better readability (groups of 2 characters)
                formatted_hex = ' '.join([hex_string[i:i+2] for i in range(0, len(hex_string), 2)])
                
                print(f"Sending chunk {i}: {formatted_hex}")
                self.client_socket.send(hex_bytes)
                success_count += 1
                print(f"Sent {len(hex_bytes)} bytes")
                
                # Small delay to allow for interrupt and server processing
                time.sleep(0.1)
            
            if self.running:
                print("-" * 60)
                print(f"Successfully sent {success_count} out of {len(hex_data_list)} 32-byte chunks to server!")
                return success_count == len(hex_data_list)
            else:
                print("Sending was cancelled")
                return False
            
        except ConnectionRefusedError:
            print("Could not connect to server. Make sure the server is running.")
            return False
        except Exception as e:
            print(f"Error sending data: {e}")
            return False
    
    def process_directory(self, directory_path, file_pattern="*.txt"):
        """Process all files in a directory matching the pattern"""
        all_hex_data = []
        
        # Find all files matching the pattern
        search_pattern = os.path.join(directory_path, file_pattern)
        files = glob.glob(search_pattern)
        
        if not files:
            print(f"No files found matching pattern: {search_pattern}")
            return []
        
        print(f"Found {len(files)} files to process:")
        print("Press Ctrl+C to cancel processing")
        print("-" * 60)
        
        for file_path in files:
            if not self.running:
                print("Processing cancelled by user")
                break
                
            print(f"\nProcessing file: {file_path}")
            hex_data = self.process_file(file_path)
            all_hex_data.extend(hex_data)
        
        return all_hex_data

    def interactive_mode(self):
        """Interactive mode for manual control"""
        print("Interactive mode started. Commands:")
        print("  'quit' or 'exit' - Shutdown client")
        print("  'status' - Show current status")
        print("  'send' - Process files and send data")
        print("  'files' - List available files")
        print("  'test' - Test with sample data")
        
        while self.running:
            try:
                command = input("\nEnter command: ").strip().lower()
                
                if command in ['quit', 'exit', 'q']:
                    print("Exiting interactive mode...")
                    break
                elif command == 'status':
                    print("Client is running")
                    print(f"Server: {self.host}:{self.port}")
                    print(f"Data directory: ./data_files/")
                elif command == 'send':
                    hex_data = self.process_directory('./data_files', '*.txt')
                    if hex_data:
                        self.send_hex_data(hex_data)
                elif command == 'files':
                    files = glob.glob('./data_files/*.txt')
                    if files:
                        print("Available files:")
                        for file in files:
                            size = os.path.getsize(file)
                            print(f"  {file} ({size} bytes)")
                    else:
                        print("No files found in data_files directory")
                elif command == 'test':
                    # Test with the provided sample line
                    test_line = "20250121 13:03:20.476 DEBUG    eventLog- 01 03 e5 5d c8 85 e2 ca 7d 00 15 0d 03 17 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
                    print(f"Testing with: {test_line}")
                    hex_numbers = self.extract_hex_from_eventlog(test_line)
                    print(f"Extracted hex: {hex_numbers}")
                    chunk = self.create_32byte_chunk(hex_numbers)
                    if chunk:
                        print(f"32-byte chunk: {chunk.hex().upper()}")
                else:
                    print("Unknown command. Available: quit, status, send, files, test")
                    
            except KeyboardInterrupt:
                print("\nExiting interactive mode...")
                break
            except Exception as e:
                print(f"Error in interactive mode: {e}")

def main():
    # Configuration
    HOST = '127.0.0.1'
    PORT = 12345
    DIRECTORY = './data_test'
    FILE_PATTERN = '*.txt'
    
    client = HexClient(HOST, PORT)
    client.running = True
    
    # Set up signal handlers for graceful shutdown
    signal.signal(signal.SIGINT, client.signal_handler)
    signal.signal(signal.SIGTERM, client.signal_handler)
    
    # Check if directory exists
    if not os.path.exists(DIRECTORY):
        print(f"Directory {DIRECTORY} does not exist. Creating it...")
        os.makedirs(DIRECTORY)
        print("Please add some text files with eventLog- data and run again.")
        client.shutdown()
        return
    
    # Choose mode
    print("Hex Client - EventLog Processor")
    print("=" * 50)
    print("Choose mode:")
    print("1. Automatic processing and sending")
    print("2. Interactive mode")
    print("Press Ctrl+C at any time to quit")
    print("=" * 50)
    
    try:
        choice = input("Enter choice (1 or 2): ").strip()
        
        if choice == "2":
            client.interactive_mode()
        else:
            # Process all files in directory
            hex_data = client.process_directory(DIRECTORY, FILE_PATTERN)
            
            if hex_data and client.running:
                print(f"\nTotal 32-byte chunks created: {len(hex_data)}")
                # Send data to server
                client.send_hex_data(hex_data)
            else:
                print("No valid hex data found or processing was cancelled.")
                
    except KeyboardInterrupt:
        print("\nOperation cancelled by user")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        client.shutdown()

if __name__ == "__main__":
    main()