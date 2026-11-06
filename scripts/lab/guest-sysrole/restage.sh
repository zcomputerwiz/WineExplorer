# Host-side (Debian distro): re-create /tmp/gsr in the guest after a reboot (tmpfs) and fix the pieces that live there.
K=/var/tmp/wex-g4-lab-credentials/vm_key
L=/mnt/d/DeepThought/ChromiumLab/guest-sysrole
SSH="ssh -i $K -p 2236 -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null debian@127.0.0.1"
SC="scp -r -i $K -P 2236 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
$SSH 'mkdir -p /tmp/gsr /tmp/gsr-out; chmod 777 /tmp/gsr-out' 2>&1 | grep -v Warning
$SC $L/probes $L/*.sh $L/*.py debian@127.0.0.1:/tmp/gsr/ 2>&1 | grep -v Warning
$SSH "sudo -n sed -i 's#/opt/wex-rt/#/opt/wex-rt2/#g' /etc/wineexplorer/sysrun.conf; grep loader /etc/wineexplorer/sysrun.conf; sudo -n python3 -I /tmp/gsr/guest-env-debug.py 'err+all,fixme+service,fixme+msi'; cp /home/debian/development-validation-20261101/MicrosoftEdgeEnterpriseX64.msi /tmp/gsr-out/ 2>&1 | tail -1; ls /tmp/gsr-out" 2>&1 | grep -v Warning
# The lab broker's approval file lives on tmpfs too (guest-broker-install.sh creates it once): without it every request is cancelled (1223).
$SSH 'sudo -n install -d -m 1777 /tmp/wexelev; sudo -n touch /tmp/wexelev/approve.all; sudo -n chmod 666 /tmp/wexelev/approve.all; ls -la /tmp/wexelev' 2>&1 | grep -v Warning
