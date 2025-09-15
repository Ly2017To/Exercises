import socket
import threading
import signal
import sys

class HexServer:
    def __init__(self, host='127.0.0.1', port=12345):
        self.host = host
        self.port = port
        self.server_socket = None
        self.running = False
        self.client_threads = []
    
    def format_hex_bytes(self, data):
        """Format bytes as hex strings with 0x prefix"""
        hex_strings = []
        for byte in data:
            hex_strings.append(f"0x{byte:02X}")
        return hex_strings
    
    def print_hex_data(self, data, address, line_number):
        """Print received data with each byte in 0x format"""
        hex_bytes = self.format_hex_bytes(data)
        
        print(f"Line {line_number} from {address}:")
        print(f"Raw bytes: {len(data)} bytes")
        print("Hex format:")
        
        # Print in rows of 8 bytes for better readability
        for i in range(0, len(hex_bytes), 8):
            row = hex_bytes[i:i+8]
            print(f"  {' '.join(row)}")
        
        print("-" * 60)
    
    def handle_client(self, client_socket, address):
        """Handle incoming hex data from a client"""
        print(f"Connection established with {address}")
        
        try:
            line_counter = 0
            while self.running:
                try:
                    # Set timeout to allow checking running status
                    client_socket.settimeout(1.0)
                    # Receive 32 bytes of data (one line's worth of hex data)
                    data = client_socket.recv(32)
                    if not data:
                        break
                    
                    line_counter += 1
                    self.print_hex_data(data, address, line_counter)
                    
                except socket.timeout:
                    # Timeout occurred, check if we should continue running
                    continue
                except ConnectionResetError:
                    print(f"Connection with {address} was reset")
                    break
                
        except Exception as e:
            print(f"Error handling client {address}: {e}")
        finally:
            client_socket.close()
            print(f"Connection with {address} closed")
    
    def signal_handler(self, sig, frame):
        """Handle keyboard interrupt signals"""
        print("\nReceived shutdown signal...")
        self.running = False
        self.shutdown()
    
    def shutdown(self):
        """Gracefully shutdown the server"""
        print("Shutting down server...")
        
        # Close all client connections
        for thread in self.client_threads:
            if thread.is_alive():
                # You might want to implement a way to signal threads to stop
                pass
        
        # Close server socket
        if self.server_socket:
            self.server_socket.close()
            print("Server socket closed")
        
        print("Server shutdown complete")
        sys.exit(0)
    
    def start(self):
        """Start the server and listen for incoming connections"""
        # Set up signal handlers for graceful shutdown
        signal.signal(signal.SIGINT, self.signal_handler)
        signal.signal(signal.SIGTERM, self.signal_handler)
        
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        # Bind to address and port
        self.server_socket.bind((self.host, self.port))
        
        # Listen for incoming connections
        self.server_socket.listen(5)
        print(f"Hex Server listening on {self.host}:{self.port}")
        print("Waiting for 32-byte hex data grouped by line...")
        print("Each byte will be displayed in 0x format")
        print("Press Ctrl+C to quit")
        print("-" * 60)
        
        self.running = True
        
        try:
            while self.running:
                try:
                    # Set timeout to allow checking running status
                    self.server_socket.settimeout(1.0)
                    
                    # Accept incoming connection
                    client_socket, address = self.server_socket.accept()
                    
                    # Create a new thread for each client
                    client_thread = threading.Thread(
                        target=self.handle_client, 
                        args=(client_socket, address),
                        daemon=True
                    )
                    client_thread.start()
                    self.client_threads.append(client_thread)
                    print(f"Active connections: {threading.active_count() - 1}")
                    
                except socket.timeout:
                    # Timeout occurred, check if we should continue running
                    continue
                except OSError:
                    # Socket might be closed during shutdown
                    if not self.running:
                        break
                    raise
                
        except KeyboardInterrupt:
            print("\nKeyboard interrupt received...")
        except Exception as e:
            print(f"Server error: {e}")
        finally:
            self.shutdown()

if __name__ == "__main__":
    server = HexServer()
    server.start()