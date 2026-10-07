import subprocess

p = '/opt/VBoxGuestAdditions-7.2.20/init/vboxadd-service'
with open(p, 'r') as f:
    lines = f.readlines()

new_lines = []
for line in lines:
    if 'start-stop-daemon --start --exec' in line and '--disable-timesync' not in line:
        line = line.strip() + ' --disable-timesync\n'
    new_lines.append(line)

with open(p, 'w') as f:
    f.writelines(new_lines)

subprocess.run(['systemctl', 'restart', 'vboxadd-service'])
