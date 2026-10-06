#!/bin/sh
# =============================================================================
#  iStoreOS / OpenWrt 上启用 NetData CPU 温度采集
#  适用于 NetData v1.x(本项目 NetData v1.33.1 实测可用)
#
#  原理:
#    1. iStoreOS 软路由 SOC 温度读自 /sys/class/thermal/thermal_zone0/temp (单位 mC)
#    2. lm-sensors 在软路由 SOC 上基本不可用,直接走 sysfs
#    3. 写一个 NetData python.d 插件,把 sysfs 的值推成 chart: thermal.cpu
#    4. ESP8266 项目 CHART_TEMP 默认就是 "thermal.cpu",无需再改 main.ino
#
#  使用:
#    ssh root@10.0.0.1
#    sh install_sensor_istoreos.sh
#
#  验证:
#    curl 'http://127.0.0.1:19999/api/v1/data?chart=thermal.cpu&points=1&format=array'
# =============================================================================

set -e

echo "=== [1/5] 验证 sysfs 温度接口 ==="
if [ ! -r /sys/class/thermal/thermal_zone0/temp ]; then
    echo "❌ /sys/class/thermal/thermal_zone0/temp 不可读"
    echo "   你的 iStoreOS 内核可能没暴露温度,先确认:"
    echo "   ls /sys/class/thermal/  有没有 thermal_zone0/1/2"
    exit 1
fi
TEMP_MC=$(cat /sys/class/thermal/thermal_zone0/temp)
echo "✓ thermal_zone0 当前读数: ${TEMP_MC} mC (= $(echo "scale=2; ${TEMP_MC}/1000" | awk -F'=' '{printf "%.2f", $2/1000}') °C)"

echo ""
echo "=== [2/5] 找到 python.d 插件目录 ==="
NETDATA_PYDIR=""
for d in /etc/netdata/python.d /usr/lib/netdata/python.d; do
    if [ -d "$d" ]; then
        NETDATA_PYDIR="$d"
        echo "✓ 使用目录: $d"
        break
    fi
done
if [ -z "$NETDATA_PYDIR" ]; then
    echo "❌ 没找到 NetData python.d 目录"
    exit 1
fi

echo ""
echo "=== [3/5] 写入 thermal.chart.py 插件 ==="
cat > "${NETDATA_PYDIR}/thermal.chart.py" <<'PYEOF'
# -*- coding: utf-8 -*-
# NetData python.d 插件 — 读取 /sys/class/thermal/thermal_zone0/temp
# 关键点:NetData python.d 加载器**强制要求类名是 Service,不是 Order/Chart 等其他名字
from bases.FrameworkServices.SimpleService import SimpleService

UPDATE_EVERY = 5
ORDER = ['cpu']
CHARTS = {
    'cpu': {
        'options': [None, 'CPU Temperature', 'celsius',
                    'CPU temperature', 'cpu', 'line'],
        'lines': [
            ['cpu', None, 'absolute'],
        ],
    },
}


class Service(SimpleService):
    def __init__(self, configuration=None, name=None):
        SimpleService.__init__(self, configuration=configuration, name=name)
        self.order = ORDER
        self.definitions = CHARTS

    @staticmethod
    def check():
        try:
            with open('/sys/class/thermal/thermal_zone0/temp') as f:
                f.read()
            return True
        except OSError as e:
            self.error('cannot read thermal_zone0: {}'.format(e))
            return False

    def get_data(self):
        data = {}
        try:
            with open('/sys/class/thermal/thermal_zone0/temp') as f:
                milli_c = int(f.read().strip())
                data['cpu'] = milli_c / 1000.0
        except (OSError, ValueError) as e:
            self.error('read error: {}'.format(e))
        return data
PYEOF
echo "✓ 已写入 ${NETDATA_PYDIR}/thermal.chart.py"

echo ""
echo "=== [4/5] 重启 NetData ==="
/etc/init.d/netdata restart
sleep 8

echo ""
echo "=== [5/5] 验证 chart ==="
echo "--- /api/v1/charts 中是否含 thermal.cpu ---"
if command -v python3 >/dev/null 2>&1; then
    curl -s 'http://127.0.0.1:19999/api/v1/charts' | python3 -c "
import json, sys
d = json.load(sys.stdin)
ids = [k for k in d['charts'] if 'thermal' in k or 'temp' in k.lower()]
print('找到 chart:', ids if ids else '(空)')
"
else
    curl -s 'http://127.0.0.1:19999/api/v1/charts' | grep -o '"thermal[^"]*"' | sort -u
fi

echo ""
echo "--- /api/v1/data?chart=thermal.cpu 测试数据 ---"
curl -s 'http://127.0.0.1:19999/api/v1/data?chart=thermal.cpu&points=1&format=array'
echo ""

echo ""
echo "如果上面显示温度数据,ESP8266 项目烧录后会自动显示。"
echo "若没显示,排查:"
echo "  1. cat /var/log/netdata/error.log | grep -i thermal"
echo "  2. /usr/lib/netdata/plugins.d/python.d.plugin thermal debug"