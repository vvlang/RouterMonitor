#ifndef __NETDATA_H
#define __NETDATA_H

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <string>

// =============================================================
//  NetData 服务配置 — 在这里集中改你的路由器设置
// =============================================================
#define NETDATA_HOST      "10.0.0.1"   // 软路由 IP
#define NETDATA_PORT      19999         // NetData 端口,默认 19999

// API 路径:
//   v1.x(本项目默认,NetData v1.0 ~ v1.45):  /api/v1/data
//   v2.x(2024+ Go 重写版):                   /api/v2/data
#define NETDATA_API_PATH  "/api/v1/data"

// =============================================================
//  chart id 配置 — 根据你的路由器上实际 chart 名调整
//  查询方式: curl http://10.0.0.1:19999/api/v1/charts | python3 -m json.tool | grep chart_id
// =============================================================
#define CHART_CPU         "system.cpu"        // CPU 占用率
#define CHART_MEMORY      "mem.available"     // 剩余内存
#define CHART_NET         "net.eth0"          // 网卡名,可能是 br-lan / pppoe-wan / eth0 / br-lan 等
#define DIM_NET_RECV      "received"
#define DIM_NET_SENT      "sent"

// 温度:由路由器侧通过 NetData statsd 推送(无需 Python)
// 路由器侧推送格式:echo "thermal_cpu:XX.XX|g" | nc -u 127.0.0.1 8125
// NetData 自动注册 chart id 为 statsd.thermal_cpu
#define CHART_TEMP        "statsd_gauge_thermal_cpu.gauge"

// 总内存(MB),用于 mem.available 换算为使用率,根据路由器实际内存改
// iStoreOS 24.10.8 路由器:4GB RAM = 4096 MiB
#define TOTAL_MEM_MB      4096.0

class NetChartData
{
public:
    int api;
    String id;
    String name;

    int view_update_every;
    int update_every;
    long first_entry;
    long last_entry;
    long before;
    long after;
    String group;
    String options_0;
    String options_1;

    JsonArray dimension_names;
    JsonArray dimension_ids;
    JsonArray latest_values;
    JsonArray view_latest_values;
    int dimensions;
    int points;
    String format;
    JsonArray result;
    double min;
    double max;
};

void parseNetDataResponse(WiFiClient &client, NetChartData &data)
{
    DynamicJsonDocument doc(4096);

    DeserializationError error = deserializeJson(doc, client);
    if (error)
    {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    data.api = doc["api"];
    data.id = doc["id"].as<String>();
    data.name = doc["name"].as<String>();
    data.view_update_every = doc["view_update_every"];
    data.update_every = doc["update_every"];
    data.first_entry = doc["first_entry"];
    data.last_entry = doc["last_entry"];
    data.after = doc["after"];
    data.before = doc["before"];
    data.group = doc["group"].as<String>();

    data.options_0 = doc["options"][0].as<String>();
    data.options_1 = doc["options"][1].as<String>();

    data.dimension_names = doc["dimension_names"];
    data.dimension_ids = doc["dimension_ids"];
    data.latest_values = doc["latest_values"];
    data.view_latest_values = doc["view_latest_values"];

    data.dimensions = doc["dimensions"];
    data.points = doc["points"];
    data.format = doc["format"].as<String>();

    JsonArray db_points_per_tier = doc["db_points_per_tier"];
    data.result = doc["result"];
    data.min = doc["min"];
    data.max = doc["max"];
}

bool getNetDataInfoWithDimension(String chartID, NetChartData &data, String dimensions_filter)
{
    String reqRes = String(NETDATA_API_PATH) + String(F("?chart=")) + chartID +
                    String(F("&format=json&points=1&group=average&gtime=0")) +
                    String(F("&options=s%7Cjsonwrap%7Cnonzero&after=-2"));
    if (dimensions_filter.length() > 0)
    {
        reqRes = reqRes + String(F("&dimensions=")) + dimensions_filter;
    }

    WiFiClient client;
    bool ret = false;

    String httpRequest = String("GET ") + reqRes + " HTTP/0.1\r\n" +
                         "Host: " + String(NETDATA_HOST) + "\r\n" +
                         "Connection: close\r\n\r\n";

    if (client.connect(NETDATA_HOST, NETDATA_PORT))
    {
        client.print(httpRequest);
        Serial.println("Sending request: ");
        Serial.println(httpRequest);

        String status_response = client.readStringUntil('\n');
        Serial.print("status_response: ");
        Serial.println(status_response);
        if (client.find("\r\n\r\n"))
        {
            Serial.println("Found Header End. Start Parsing.");
        }

        // 探测 body 是不是 JSON。
        // NetData v1.x 在 chart 不存在时返回纯文本 "Chart is not found: ..." 或
        // "No data: ...",v2.x 返回 JSON {"error": ...};这里只挑 JSON 解析。
        char peek = client.peek();
        // 跳过 body 开头的空白字符
        while (peek == ' ' || peek == '\r' || peek == '\n' || peek == '\t' || peek == '\0')
        {
            if (!client.available())
            {
                break;
            }
            client.read();
            peek = client.peek();
        }
        if (peek != '{')
        {
            String body = client.readString();
            Serial.print(F("Non-JSON response (chart missing?): "));
            Serial.println(body);
            client.stop();
            return false;
        }

        parseNetDataResponse(client, data);
        ret = true;
    }
    else
    {
        Serial.println(" connection failed!");
    }
    client.stop();
    return ret;
}

bool getNetDataInfo(String chartID, NetChartData &data)
{
    return getNetDataInfoWithDimension(chartID, data, "");
}

#endif