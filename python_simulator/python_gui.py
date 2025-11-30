import tkinter as tk
from tkinter import messagebox, simpledialog
import socket
import re

TCP_IP = '127.0.0.1'
TCP_PORT = 5000
BUFFER_SIZE = 1024

class SmartHomeApp:
    def __init__(self, master):
        self.master = master
        self.master.title("Smart Home Control System")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.sock.connect((TCP_IP, TCP_PORT))
        except Exception as e:
            messagebox.showerror("Error", f"Cannot connect server: {e}")
            master.destroy()
            return

        self.token = None
        self.app_id = None
        self.username = None
        self.device_list_data = []
        self.current_device = None
        self.current_device_type = None

        self.create_login_page()

    # ------------------ Trang đăng nhập ------------------
    def create_login_page(self):
        self.clear_window()
        tk.Label(self.master, text="Username:").grid(row=0,column=0, padx=5, pady=5)
        tk.Label(self.master, text="Password:").grid(row=1,column=0, padx=5, pady=5)
        self.entry_user = tk.Entry(self.master, width=20)
        self.entry_pass = tk.Entry(self.master, show="*", width=20)
        self.entry_user.grid(row=0,column=1, padx=5, pady=5)
        self.entry_pass.grid(row=1,column=1, padx=5, pady=5)
        tk.Button(self.master, text="Login", command=self.login, width=15).grid(row=2,column=0, padx=5, pady=5)
        tk.Button(self.master, text="Register", command=self.register, width=15).grid(row=2,column=1, padx=5, pady=5)

    def login(self):
        username = self.entry_user.get()
        password = self.entry_pass.get()
        if not username or not password:
            messagebox.showwarning("Warning","Enter username and password")
            return
        # Generate app_id
        import time
        app_id = f"{username}_{int(time.time())}"
        self.sock.send(f"LOGIN {username} {password} {app_id}\r\n".encode())
        resp = self.sock.recv(BUFFER_SIZE).decode()
        if resp.startswith("200 OK"):
            # Extract token from response: "200 OK Token=xxx AppID=yyy"
            token_match = re.search(r'Token=(\S+)', resp)
            app_id_match = re.search(r'AppID=(\S+)', resp)
            if token_match:
                self.token = token_match.group(1)
            if app_id_match:
                self.app_id = app_id_match.group(1)
            self.username = username
            self.create_device_page()
        else:
            messagebox.showerror("Login Failed", resp.strip())

    def register(self):
        username = self.entry_user.get()
        password = self.entry_pass.get()
        if not username or not password:
            messagebox.showwarning("Warning","Enter username and password")
            return
        # Lưu vào file users.txt trên server để test (offline)
        try:
            with open("users.txt","a") as f:
                f.write(f"{username} {password}\n")
            messagebox.showinfo("Register", "User registered. Please login.")
        except Exception as e:
            messagebox.showerror("Error", f"Registration failed: {e}")

    # ------------------ Trang quản lý thiết bị ------------------
    def create_device_page(self):
        self.clear_window()
        
        # Header
        tk.Label(self.master, text=f"Logged in as: {self.username}", font=("Arial", 10, "bold")).grid(row=0,column=0,columnspan=3, padx=5, pady=5)
        if self.token:
            tk.Label(self.master, text=f"Token: {self.token[:16]}...", font=("Arial", 8)).grid(row=1,column=0,columnspan=3, padx=5, pady=2)
        
        # Device list section
        tk.Label(self.master, text="My Devices:", font=("Arial", 9, "bold")).grid(row=2,column=0,columnspan=2, sticky="w", padx=5)
        tk.Button(self.master, text="Scan Devices", command=self.scan_devices, width=15).grid(row=2,column=2, padx=5, pady=5)
        self.device_list = tk.Listbox(self.master, width=40, height=8)
        self.device_list.grid(row=3,column=0,columnspan=3, padx=5, pady=5)
        tk.Button(self.master, text="Connect Device", command=self.connect_device, width=15).grid(row=4,column=0, padx=5, pady=5)
        tk.Button(self.master, text="Logout", command=self.logout, width=15).grid(row=4,column=2, padx=5, pady=5)

        # Device control section
        tk.Label(self.master, text="Device Control:", font=("Arial", 9, "bold")).grid(row=5,column=0,columnspan=3, sticky="w", padx=5, pady=(10,5))
        
        # Power control
        self.btn_power_on = tk.Button(self.master, text="Power ON", command=lambda: self.set_power("ON"), state="disabled", width=12)
        self.btn_power_on.grid(row=6,column=0, padx=2, pady=2)
        self.btn_power_off = tk.Button(self.master, text="Power OFF", command=lambda: self.set_power("OFF"), state="disabled", width=12)
        self.btn_power_off.grid(row=6,column=1, padx=2, pady=2)
        
        # Fan Speed control
        tk.Label(self.master,text="Fan Speed:").grid(row=7,column=0, sticky="w", padx=5)
        self.fan_speed = tk.StringVar(value="1")
        self.fan_speed_menu = tk.OptionMenu(self.master,self.fan_speed,"1","2","3")
        self.fan_speed_menu.config(state="disabled", width=10)
        self.fan_speed_menu.grid(row=7,column=1, padx=5, pady=2)
        self.btn_set_speed = tk.Button(self.master,text="Set Speed",command=self.set_speed,state="disabled", width=12)
        self.btn_set_speed.grid(row=7,column=2, padx=2, pady=2)

        # AC Mode control
        tk.Label(self.master,text="AC Mode:").grid(row=8,column=0, sticky="w", padx=5)
        self.ac_mode = tk.StringVar(value="COOL")
        self.ac_mode_menu = tk.OptionMenu(self.master,self.ac_mode,"COOL","HEAT","DRY")
        self.ac_mode_menu.config(state="disabled", width=10)
        self.ac_mode_menu.grid(row=8,column=1, padx=5, pady=2)
        self.btn_set_mode = tk.Button(self.master,text="Set Mode",command=self.set_mode,state="disabled", width=12)
        self.btn_set_mode.grid(row=8,column=2, padx=2, pady=2)
        
        # AC Temperature control
        tk.Label(self.master,text="AC Temperature (18-30):").grid(row=9,column=0, sticky="w", padx=5)
        self.ac_temp = tk.Entry(self.master, width=12)
        self.ac_temp.insert(0, "25")
        self.ac_temp.config(state="disabled")
        self.ac_temp.grid(row=9,column=1, padx=5, pady=2)
        self.btn_set_temp = tk.Button(self.master,text="Set Temp",command=self.set_temp,state="disabled", width=12)
        self.btn_set_temp.grid(row=9,column=2, padx=2, pady=2)
        
        # Timer control
        tk.Label(self.master,text="Timer (seconds):").grid(row=10,column=0, sticky="w", padx=5)
        self.timer_seconds = tk.Entry(self.master, width=12)
        self.timer_seconds.insert(0, "60")
        self.timer_seconds.config(state="disabled")
        self.timer_seconds.grid(row=10,column=1, padx=5, pady=2)
        self.timer_action = tk.StringVar(value="ON")
        self.timer_action_menu = tk.OptionMenu(self.master, self.timer_action, "ON", "OFF")
        self.timer_action_menu.config(state="disabled", width=8)
        self.timer_action_menu.grid(row=10,column=2, padx=2, pady=2)
        self.btn_set_timer = tk.Button(self.master,text="Set Timer",command=self.set_timer,state="disabled", width=12)
        self.btn_set_timer.grid(row=11,column=1, padx=2, pady=2)
        
        # Status display
        self.status_label = tk.Label(self.master, text="Status: Not connected", fg="gray", font=("Arial", 9))
        self.status_label.grid(row=12,column=0,columnspan=3, padx=5, pady=5)
        
        # Get Status button
        self.btn_status = tk.Button(self.master,text="Get Status",command=self.get_status,state="disabled", width=15)
        self.btn_status.grid(row=13,column=1, padx=5, pady=5)

    def scan_devices(self):
        self.sock.send(f"SCAN {self.username}\r\n".encode())
        data = self.sock.recv(BUFFER_SIZE).decode()
        self.device_list.delete(0,tk.END)
        self.device_list_data.clear()
        for line in data.strip().split("\n"):
            if line.strip() and not line.startswith("NO_DEVICES"):
                self.device_list.insert(tk.END,line)
                self.device_list_data.append(line.strip())

    def connect_device(self):
        sel = self.device_list.curselection()
        if not sel:
            messagebox.showwarning("Warning","Select a device")
            return
        line = self.device_list_data[sel[0]]
        parts = line.split()
        device_id = parts[0]
        device_type = parts[1] if len(parts) > 1 else "Unknown"
        pwd = simpledialog.askstring("Device Password", "Enter device password:", show="*")
        if not pwd:
            return
        self.sock.send(f"CONNECT {device_id} {pwd}\r\n".encode())
        resp = self.sock.recv(BUFFER_SIZE).decode()
        if resp.startswith("200"):
            self.current_device = device_id
            self.current_device_type = device_type
            # Enable controls
            self.btn_power_on.config(state="normal")
            self.btn_power_off.config(state="normal")
            self.btn_status.config(state="normal")
            if device_type == "Fan":
                self.fan_speed_menu.config(state="normal")
                self.btn_set_speed.config(state="normal")
            elif device_type == "AC":
                self.ac_mode_menu.config(state="normal")
                self.ac_temp.config(state="normal")
                self.btn_set_mode.config(state="normal")
                self.btn_set_temp.config(state="normal")
            self.timer_seconds.config(state="normal")
            self.timer_action_menu.config(state="normal")
            self.btn_set_timer.config(state="normal")
            
            # Extract device info from response
            info_text = resp.replace("200 OK CONNECTED ", "").strip()
            messagebox.showinfo("Connected", f"Connected to {device_id}\n\n{info_text}")
            self.status_label.config(text=f"Connected: {device_id} ({device_type})", fg="green")
        else:
            messagebox.showerror("Failed", resp.strip())

    # ------------------ Device control ------------------
    def set_power(self, state):
        if not self.current_device: return
        self.sock.send(f"POWER {self.current_device} {state}\r\n".encode())
        resp = self.sock.recv(BUFFER_SIZE).decode()
        if resp.startswith("200"):
            # Extract power from response
            power_match = re.search(r'POWER:([\d.]+)W', resp)
            power = power_match.group(1) if power_match else "N/A"
            messagebox.showinfo("Power", f"Device {state}\nPower consumption: {power}W")
            self.get_status()  # Refresh status
        else:
            messagebox.showerror("Error", resp.strip())

    def set_speed(self):
        if not self.current_device: return
        level = self.fan_speed.get()
        self.sock.send(f"SPEED {self.current_device} {level}\r\n".encode())
        resp = self.sock.recv(BUFFER_SIZE).decode()
        if resp.startswith("200"):
            power_match = re.search(r'POWER:([\d.]+)W', resp)
            power = power_match.group(1) if power_match else "N/A"
            messagebox.showinfo("Fan Speed", f"Speed set to {level}\nPower consumption: {power}W")
            self.get_status()
        else:
            messagebox.showerror("Error", resp.strip())

    def set_mode(self):
        if not self.current_device: return
        mode = self.ac_mode.get()
        self.sock.send(f"MODE {self.current_device} {mode}\r\n".encode())
        resp = self.sock.recv(BUFFER_SIZE).decode()
        if resp.startswith("200"):
            power_match = re.search(r'POWER:([\d.]+)W', resp)
            power = power_match.group(1) if power_match else "N/A"
            messagebox.showinfo("AC Mode", f"Mode set to {mode}\nPower consumption: {power}W")
            self.get_status()
        else:
            messagebox.showerror("Error", resp.strip())

    def set_temp(self):
        if not self.current_device: return
        try:
            temp = int(self.ac_temp.get())
            if temp < 18 or temp > 30:
                messagebox.showerror("Error","Temperature must be between 18-30")
                return
        except:
            messagebox.showerror("Error","Invalid temperature")
            return
        self.sock.send(f"TEMP {self.current_device} {temp}\r\n".encode())
        resp = self.sock.recv(BUFFER_SIZE).decode()
        if resp.startswith("200"):
            power_match = re.search(r'POWER:([\d.]+)W', resp)
            power = power_match.group(1) if power_match else "N/A"
            messagebox.showinfo("AC Temperature", f"Temperature set to {temp}°C\nPower consumption: {power}W")
            self.get_status()
        else:
            messagebox.showerror("Error", resp.strip())

    def set_timer(self):
        if not self.current_device: return
        try:
            seconds = int(self.timer_seconds.get())
            if seconds < 0:
                messagebox.showerror("Error","Timer must be positive")
                return
        except:
            messagebox.showerror("Error","Invalid timer value")
            return
        action = self.timer_action.get()
        self.sock.send(f"TIMER {self.current_device} {action} {seconds}\r\n".encode())
        resp = self.sock.recv(BUFFER_SIZE).decode()
        if resp.startswith("200"):
            messagebox.showinfo("Timer", f"Timer set: {action} in {seconds} seconds")
            self.get_status()
        else:
            messagebox.showerror("Error", resp.strip())

    def get_status(self):
        if not self.current_device: return
        self.sock.send(f"STATUS {self.current_device}\r\n".encode())
        resp = self.sock.recv(BUFFER_SIZE).decode()
        if resp.startswith("200"):
            # Parse status response
            status_text = resp.replace("200 OK STATUS ", "").strip()
            # Extract power
            power_match = re.search(r'POWER:([\d.]+)W', status_text)
            power = power_match.group(1) if power_match else "0.00"
            # Extract timer if exists
            timer_match = re.search(r'TIMER:(\S+)(?:\s+in\s+(\d+)\s+seconds)?', status_text)
            timer_info = ""
            if timer_match:
                timer_action = timer_match.group(1)
                timer_remaining = timer_match.group(2) if timer_match.group(2) else ""
                if timer_remaining:
                    timer_info = f"\nTimer: {timer_action} in {timer_remaining} seconds"
                else:
                    timer_info = f"\nTimer: {timer_action}"
            
            messagebox.showinfo("Device Status", f"{status_text}\n\nPower Consumption: {power}W{timer_info}")
            self.status_label.config(text=f"Status: {status_text.split('STATE:')[1].split()[0] if 'STATE:' in status_text else 'Unknown'} | Power: {power}W", fg="blue")
        else:
            messagebox.showerror("Error", resp.strip())

    def logout(self):
        self.current_device = None
        self.current_device_type = None
        self.token = None
        self.app_id = None
        self.username = None
        self.create_login_page()

    # ------------------ Utility ------------------
    def clear_window(self):
        for w in self.master.winfo_children():
            w.destroy()

# ---------------- Main ----------------
if __name__ == "__main__":
    root = tk.Tk()
    root.geometry("500x600")
    app = SmartHomeApp(root)
    root.mainloop()