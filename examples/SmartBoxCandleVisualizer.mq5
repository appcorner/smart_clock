//+------------------------------------------------------------------+
//| SmartBoxCandleVisualizer.mq5                                       |
//| Expert Advisor for MetaTrader 5                                   |
//| Sends candlestick data to SmartBox Dashboard                      |
//+------------------------------------------------------------------+
#property copyright "SmartBox Dashboard"
#property link      "https://github.com/appcorner/smart_clock"
#property version   "1.00"
#property description "Sends real-time candlestick data to SmartBox dashboard via HTTP API"

//--- Input parameters
input string     g_dashboardIP         = "192.168.1.148";     // Dashboard IP address
input ushort     g_dashboardPort       = 80;                  // Dashboard port
input string     g_dashboardUsername   = "admin";             // API username
input string     g_dashboardPassword   = "smartclock";        // API password
input string     g_titleText           = "XAUUSD";            // Chart title
input color      g_titleColor          = clrOrange;           // Title color
input int        g_candleCount         = 40;                  // Number of candles to send (1-40)
input int        g_updateIntervalMins  = 1;                   // Update interval in minutes
input bool       g_enableLogging       = true;                // Enable console logging

//--- Global variables
int g_lastUpdateTime = 0;

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit()
{
   if(g_candleCount < 1 || g_candleCount > 40) {
      Print("[ERROR] Candle count must be between 1 and 40");
      return INIT_PARAMETERS_INCORRECT;
   }

   if(g_updateIntervalMins < 1 || g_updateIntervalMins > 1440) {
      Print("[ERROR] Update interval must be between 1 and 1440 minutes");
      return INIT_PARAMETERS_INCORRECT;
   }

   Print("[INIT] SmartBox Candle Visualizer initialized");
   Print("[INFO] Dashboard: http://", g_dashboardIP, ":", g_dashboardPort);
   Print("[INFO] Update interval: ", g_updateIntervalMins, " minutes");
   Print("[INFO] Candlestick count: ", g_candleCount);

   return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
//| Expert deinitialization function                                 |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
   Print("[DEINIT] Expert advisor stopped. Reason: ", reason);
}

//+------------------------------------------------------------------+
//| Expert tick function                                              |
//+------------------------------------------------------------------+
void OnTick()
{
   int currentTime = (int)TimeCurrent();
   int timeSinceLastUpdate = currentTime - g_lastUpdateTime;

   if(timeSinceLastUpdate >= (g_updateIntervalMins * 60)) {
      SendCandleData();
      g_lastUpdateTime = currentTime;
   }
}

//+------------------------------------------------------------------+
//| Build JSON payload manually and send to dashboard                |
//+------------------------------------------------------------------+
void SendCandleData()
{
   string json = "{\"mode\":\"dashboard\",\"widgets\":[";

   // Title widget
   json += "{\"type\":\"title\",\"text\":\"";
   json += g_titleText;
   json += "\",\"color\":\"";
   json += ColorToNamedString(g_titleColor);
   json += "\"}";

   // Candle data array
   json += ",{\"type\":\"candles\",\"data\":[";

   for(int i = g_candleCount - 1; i >= 0; i--) {
      if(i < g_candleCount - 1) json += ",";

      double open = iOpen(_Symbol, _Period, i);
      double high = iHigh(_Symbol, _Period, i);
      double low = iLow(_Symbol, _Period, i);
      double close = iClose(_Symbol, _Period, i);

      json += "[";
      json += DoubleToString(open, _Digits);
      json += ",";
      json += DoubleToString(high, _Digits);
      json += ",";
      json += DoubleToString(low, _Digits);
      json += ",";
      json += DoubleToString(close, _Digits);
      json += "]";
   }

   json += "],\"x\":5,\"y\":40,\"w\":230,\"h\":140,\"color\":\"green\",\"color2\":\"red\",\"axis\":true}";
   
   // Price hline widget
   string priceStr = StringFormat("%.2f", iClose(_Symbol, _Period, 0));
   json += ",{\"type\":\"hline\",\"value\": ";
   json += priceStr;
   json += ",\"ref\":\"candles\",\"color\":\"yellow\"}";
   
   json += ",{\"type\":\"text\",\"text\":\"Price : ";
   json += priceStr;
   json += "\",\"y\":180,\"size\":1,\"color\":\"yellow\"}";
   
   // Balance text widget
   string balanceStr = StringFormat("%.2f", AccountInfoDouble(ACCOUNT_BALANCE));
   json += ",{\"type\":\"text\",\"text\":\"Balance : ";
   json += balanceStr;
   json += "\",\"y\":195,\"size\":1,\"color\":\"white\"}";

   // Equity text widget
   string equiutyStr = StringFormat("%.2f", AccountInfoDouble(ACCOUNT_EQUITY));
   json += ",{\"type\":\"text\",\"text\":\"Equity : ";
   json += equiutyStr;
   json += "\",\"y\":210,\"size\":1,\"color\":\"green\"}";

   json += "]}";

   if(g_enableLogging) {
      Print("[DEBUG] JSON Payload: ", json);
   }

   SendHTTPRequest(json);
}

//+------------------------------------------------------------------+
//| Send HTTP POST request with Basic Authentication                 |
//+------------------------------------------------------------------+
void SendHTTPRequest(const string &jsonData)
{
   string url = "http://" + g_dashboardIP + ":" + IntegerToString(g_dashboardPort) + "/api/draw";
   string headers = "Content-Type: application/json\r\n";

   // Create Basic Auth header
   string credentials = g_dashboardUsername + ":" + g_dashboardPassword;
   string authHeader = "Authorization: Basic " + Base64Encode(credentials) + "\r\n";
   headers += authHeader;

   // Convert string to uchar array
   uchar request[];
   uchar response[];
   string result_headers;

   StringToCharArray(jsonData, request);

   // Make HTTP request - use 7-parameter version
   int respCode = WebRequest("POST", url, headers, 3000, request, response, result_headers);

   if(respCode == 200) {
      if(g_enableLogging) {
         Print("[SUCCESS] Data sent to dashboard. Response: 200 OK");
      }
   } else {
      Print("[ERROR] HTTP Error: ", respCode);
   }
}

//+------------------------------------------------------------------+
//| Simple Base64 Encoding                                            |
//+------------------------------------------------------------------+
string Base64Encode(const string &source)
{
   string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   string encoded = "";
   int len = StringLen(source);

   for(int i = 0; i < len; i += 3) {
      uchar byte1 = (uchar)source[i];
      uchar byte2 = (i + 1 < len) ? (uchar)source[i + 1] : 0;
      uchar byte3 = (i + 2 < len) ? (uchar)source[i + 2] : 0;

      int b1 = (byte1 >> 2) & 0x3F;
      int b2 = (((byte1 & 0x03) << 4) | (byte2 >> 4)) & 0x3F;
      int b3 = (((byte2 & 0x0F) << 2) | (byte3 >> 6)) & 0x3F;
      int b4 = byte3 & 0x3F;

      encoded += StringSubstr(alphabet, b1, 1);
      encoded += StringSubstr(alphabet, b2, 1);
      encoded += (i + 1 < len) ? StringSubstr(alphabet, b3, 1) : "=";
      encoded += (i + 2 < len) ? StringSubstr(alphabet, b4, 1) : "=";
   }

   return encoded;
}

//+------------------------------------------------------------------+
//| Convert color to named string for dashboard                       |
//+------------------------------------------------------------------+
string ColorToNamedString(color col)
{
   switch(col) {
      case clrRed:        return "red";
      case clrGreen:      return "green";
      case clrBlue:       return "blue";
      case clrYellow:     return "yellow";
      case clrCyan:       return "cyan";
      case clrMagenta:    return "magenta";
      case clrOrange:     return "orange";
      case clrWhite:      return "white";
      case clrGray:       return "grey";
      case clrPurple:     return "purple";
      default:            return "orange";
   }
}

//+------------------------------------------------------------------+