#include "EposServer.h"
#include <stdlib.h>
#include <string.h>
#include <WebServer.h>
#include "BleConfig.h"
#include "Config.h"
#include "Printer.h"

static WebServer server(80);
static const char *firmwareBuild = "epos-img-aspect-v5";
static const uint16_t printerDots = 384;
static String lastPrintCode;
static String lastRequestInfo;
static String rawPayload;

static String xmlEscape(const String &value)
{
    String out = value;
    out.replace("&", "&amp;");
    out.replace("\"", "&quot;");
    out.replace("<", "&lt;");
    out.replace(">", "&gt;");
    return out;
}

static void cors()
{
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "*");
    server.sendHeader("Access-Control-Allow-Private-Network", "true");
}

static String responseXml(bool success, const String &code)
{
    return "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><response xmlns=\"http://www.epson-pos.com/schemas/2011/03/epos-print\" success=\"" + String(success ? "true" : "false") + "\" code=\"" + xmlEscape(code) + "\" status=\"0\" battery=\"0\"/></s:Body></s:Envelope>";
}

static void setCode(const String &code)
{
    lastPrintCode = code;
}

static int findClose(const String &xml, int from)
{
    bool quote = false;
    char q = 0;
    for (int i = from; i < (int)xml.length(); i++)
    {
        char c = xml[i];
        if ((c == '"' || c == '\'') && (!quote || c == q))
        {
            quote = !quote;
            q = quote ? c : 0;
        }
        if (!quote && c == '>')
            return i;
    }
    return -1;
}

static String attr(const String &tag, const String &name, const String &fallback = "")
{
    String p1 = name + "=\"";
    String p2 = name + "='";
    int start = tag.indexOf(p1);
    char end = '"';
    if (start < 0)
    {
        start = tag.indexOf(p2);
        end = '\'';
        if (start < 0)
            return fallback;
    }
    start += name.length() + 2;
    int stop = tag.indexOf(end, start);
    return stop < 0 ? fallback : tag.substring(start, stop);
}

static String localName(String name)
{
    int colon = name.indexOf(':');
    if (colon >= 0)
        name = name.substring(colon + 1);
    name.toLowerCase();
    return name;
}

static int tagNameEnd(const String &tag)
{
    int end = tag.indexOf(' ');
    int slash = tag.indexOf('/');
    int gt = tag.indexOf('>');
    if (end < 0 || (slash > 0 && slash < end))
        end = slash;
    if (end < 0 || (gt > 0 && gt < end))
        end = gt;
    return end;
}

static bool findEposRoot(const String &xml, int &bodyStart, int &bodyEnd)
{
    int pos = 0;
    while (true)
    {
        int open = xml.indexOf('<', pos);
        if (open < 0)
            return false;
        if (open + 1 >= (int)xml.length() || xml[open + 1] == '/' || xml[open + 1] == '?' || xml[open + 1] == '!')
        {
            pos = open + 1;
            continue;
        }
        int close = findClose(xml, open);
        if (close < 0)
            return false;
        String tag = xml.substring(open, close + 1);
        int end = tagNameEnd(tag);
        if (end > 1)
        {
            String rawName = tag.substring(1, end);
            if (localName(rawName) == "epos-print")
            {
                bodyStart = close;
                bodyEnd = xml.indexOf("</" + rawName + ">", close);
                return bodyEnd >= 0;
            }
        }
        pos = close + 1;
    }
}

static bool flag(const String &tag, const String &name)
{
    String v = attr(tag, name, "false");
    v.toLowerCase();
    return v == "true" || v == "1";
}

static String decodeEntities(String s)
{
    s.replace("&lt;", "<");
    s.replace("&gt;", ">");
    s.replace("&quot;", "\"");
    s.replace("&apos;", "'");
    s.replace("&#10;", "\n");
    s.replace("&#x0a;", "\n");
    s.replace("&amp;", "&");
    return s;
}

static int b64(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

static void scaleImageRow(const uint8_t *src, uint16_t width, uint16_t outWidth, uint8_t *dst)
{
    uint16_t outBytes = (outWidth + 7) / 8;
    memset(dst, 0, outBytes);
    for (uint16_t x = 0; x < outWidth; x++)
    {
        uint16_t sx = width > outWidth ? (uint32_t)x * width / outWidth : x;
        if (src[sx >> 3] & (0x80 >> (sx & 7)))
            dst[x >> 3] |= 0x80 >> (x & 7);
    }
}

static bool printBase64Image(const String &xml, int start, int end, uint16_t width, uint16_t height)
{
    uint16_t srcBytes = (width + 7) / 8;
    size_t expected = (size_t)srcBytes * height;
    uint16_t outWidth = width > printerDots ? printerDots : width;
    uint16_t outHeight = width > outWidth ? ((uint32_t)height * outWidth + width / 2) / width : height;
    if (!outHeight)
        outHeight = 1;
    uint16_t outBytes = (outWidth + 7) / 8;
    const uint16_t bandRows = 24;
    uint8_t *src = (uint8_t *)calloc(srcBytes, 1);
    uint8_t *out = (uint8_t *)calloc((size_t)outBytes * bandRows, 1);
    if (!src || !out)
    {
        free(src);
        free(out);
        setCode("IMG_NO_MEM_BAND");
        return false;
    }

    uint32_t val = 0;
    int bits = 0;
    size_t decoded = 0;
    uint16_t srcLine = 0;
    uint16_t outLine = 0;
    uint16_t bandLine = 0;
    for (int i = start; i < end; i++)
    {
        if (xml[i] == '=')
            break;
        int d = b64(xml[i]);
        if (d < 0)
            continue;
        val = (val << 6) | d;
        bits += 6;
        if (bits < 8)
            continue;
        bits -= 8;
        uint8_t byte = (uint8_t)((val >> bits) & 0xFF);
        if (decoded < expected)
            src[decoded % srcBytes] = byte;
        decoded++;
        if (decoded <= expected && decoded % srcBytes == 0)
        {
            while (outLine < outHeight && (uint32_t)outLine * height / outHeight == srcLine)
            {
                scaleImageRow(src, width, outWidth, out + (size_t)bandLine * outBytes);
                bandLine++;
                outLine++;
                if (bandLine == bandRows)
                {
                    printer.rasterBytes(outBytes, bandLine, out, (size_t)outBytes * bandLine);
                    bandLine = 0;
                }
            }
            memset(src, 0, srcBytes);
            srcLine++;
        }
        val &= bits ? ((1UL << bits) - 1) : 0;
    }

    if (bandLine)
        printer.rasterBytes(outBytes, bandLine, out, (size_t)outBytes * bandLine);

    if (decoded < expected)
    {
        free(src);
        free(out);
        setCode(String("IMG_SHORT_") + String((unsigned)decoded) + "_" + String((unsigned)expected));
        return false;
    }

    free(src);
    free(out);
    setCode(String("IMG_OK_ASPECT_") + String(outWidth) + "x" + String(outHeight) + "_SRC_" + String(width) + "x" + String(height));
    return true;
}

static void applyTextTag(const String &tag)
{
    String align = attr(tag, "align");
    if (align.length())
        printer.align(align);
    String font = attr(tag, "font");
    if (font.length())
        printer.font(font);
    uint8_t width = attr(tag, "width", flag(tag, "dw") ? "2" : "1").toInt();
    uint8_t height = attr(tag, "height", flag(tag, "dh") ? "2" : "1").toInt();
    printer.scale(width ? width : 1, height ? height : 1);
    printer.bold(flag(tag, "bold"));
    printer.inverse(flag(tag, "reverse"));
    printer.underline(flag(tag, "ul") || flag(tag, "underline"));
    String linespc = attr(tag, "linespc");
    if (linespc.length())
        printer.lineHeight((uint8_t)constrain(linespc.toInt(), 0, 255));
}

static bool printEpos(const String &xml)
{
    pauseBleAdvertising();
    ensurePrinterReady();
    int bodyStart = 0;
    int bodyEnd = 0;
    if (!findEposRoot(xml, bodyStart, bodyEnd))
    {
        Serial.println("epos root not found");
        setCode("NO_EPOS_ROOT");
        resumeBleAdvertising();
        return false;
    }
    Serial.printf("epos root start=%d end=%d\n", bodyStart, bodyEnd);
    int pos = bodyStart + 1;
    while (pos < bodyEnd)
    {
        int open = xml.indexOf('<', pos);
        if (open < 0 || open >= bodyEnd)
            break;
        if (xml[open + 1] == '/')
        {
            pos = open + 2;
            continue;
        }
        int close = findClose(xml, open);
        if (close < 0)
        {
            setCode("BAD_TAG");
            resumeBleAdvertising();
            return false;
        }
        String tag = xml.substring(open, close + 1);
        int nameEnd = tagNameEnd(tag);
        if (nameEnd <= 1)
        {
            setCode("BAD_NAME");
            resumeBleAdvertising();
            return false;
        }
        String rawName = tag.substring(1, nameEnd);
        String name = localName(rawName);
        bool selfClosing = tag.endsWith("/>");
        int endTag = selfClosing ? close : xml.indexOf("</" + rawName + ">", close);
        if (!selfClosing && endTag < 0)
        {
            setCode(String("NO_END_") + name);
            resumeBleAdvertising();
            return false;
        }
        int textStart = close + 1;
        if (name == "text")
        {
            String text = selfClosing || endTag < 0 ? "" : decodeEntities(xml.substring(textStart, endTag));
            Serial.printf("epos text len=%u\n", (unsigned)text.length());
            applyTextTag(tag);
            printer.text(text);
        }
        else if (name == "feed")
        {
            Serial.printf("epos feed line=%s\n", attr(tag, "line", "1").c_str());
            printer.feed((uint8_t)constrain(attr(tag, "line", "1").toInt(), 0, 255));
        }
        else if (name == "cut")
        {
            Serial.println("epos cut");
            printer.cut();
        }
        else if (name == "barcode")
        {
            String text = selfClosing || endTag < 0 ? "" : decodeEntities(xml.substring(textStart, endTag));
            Serial.printf("epos barcode len=%u\n", (unsigned)text.length());
            printer.barcode(attr(tag, "type", "code128"), text);
        }
        else if (name == "qrcode" || name == "symbol")
        {
            String text = selfClosing || endTag < 0 ? "" : decodeEntities(xml.substring(textStart, endTag));
            Serial.printf("epos qrcode len=%u\n", (unsigned)text.length());
            printer.qrcode(text, attr(tag, "size", "4").toInt());
        }
        else if (name == "image")
        {
            uint16_t width = attr(tag, "width", "384").toInt();
            uint16_t height = attr(tag, "height", "0").toInt();
            String align = attr(tag, "align");
            if (align.length())
                printer.align(align);
            Serial.printf("epos image tag width=%u height=%u chars=%u\n", width, height, (unsigned)(endTag - textStart));
            if (!width || !height)
            {
                setCode("BAD_IMAGE_SIZE");
                resumeBleAdvertising();
                return false;
            }
            if (!printBase64Image(xml, textStart, endTag, width, height))
            {
                resumeBleAdvertising();
                return false;
            }
        }
        pos = selfClosing ? close + 1 : endTag + rawName.length() + 3;
    }
    printer.flush();
    resumeBleAdvertising();
    return true;
}

static String bodyArg()
{
    if (server.hasArg("plain"))
        return server.arg("plain");
    if (server.hasArg("printdata"))
        return server.arg("printdata");
    for (uint8_t i = 0; i < server.args(); i++)
    {
        String n = server.argName(i);
        if (n.indexOf("<epos-print") >= 0 || n.indexOf("<s:Envelope") >= 0)
            return n;
        String v = server.arg(i);
        if (v.indexOf("<epos-print") >= 0 || v.indexOf("<s:Envelope") >= 0)
            return v;
    }
    return "";
}

static void handleRawBody()
{
    HTTPRaw &raw = server.raw();
    if (raw.status == RAW_START)
    {
        rawPayload = "";
        rawPayload.reserve(60000);
        lastRequestInfo = "raw=start";
    }
    else if (raw.status == RAW_WRITE)
    {
        rawPayload.concat((const char *)raw.buf, raw.currentSize);
        lastRequestInfo = String("raw=write total=") + String((unsigned)raw.totalSize) + " current=" + String((unsigned)raw.currentSize);
    }
    else if (raw.status == RAW_END)
    {
        lastRequestInfo = String("raw=end total=") + String((unsigned)raw.totalSize) + " body=" + String(rawPayload.length());
    }
    else if (raw.status == RAW_ABORTED)
    {
        setCode("RAW_ABORTED");
        lastRequestInfo = String("raw=aborted total=") + String((unsigned)raw.totalSize);
    }
}

static void handleOptions()
{
    cors();
    server.send(204, "text/plain", "");
}

static void handlePrint()
{
    cors();
    lastPrintCode = "";
    String payloadArg;
    const String *payload = &rawPayload;
    if (!rawPayload.length())
    {
        payloadArg = bodyArg();
        payload = &payloadArg;
    }
    String handlerInfo = String("uri=") + server.uri() + " args=" + String(server.args()) + " body=" + String(payload->length()) + " raw=" + String(rawPayload.length()) + " len=" + server.header("Content-Length") + " type=" + server.header("Content-Type");
    lastRequestInfo = lastRequestInfo.startsWith("raw=") ? lastRequestInfo + " " + handlerInfo : handlerInfo;
    Serial.printf("http print uri=%s args=%u bytes=%u\n", server.uri().c_str(), (unsigned)server.args(), (unsigned)payload->length());
    if (!payload->length())
    {
        Serial.println("http empty post: status ok");
        setCode(String("EMPTY_BODY_ARGS_") + String(server.args()) + "_RAW_" + String(rawPayload.length()));
        server.send(200, "text/xml", responseXml(true, ""));
        return;
    }
    if (payload->indexOf("<image") >= 0)
    {
        server.send(200, "text/xml", responseXml(true, ""));
        server.client().flush();
        delay(20);
        bool ok = printEpos(*payload);
        if (!ok && !lastPrintCode.length())
            setCode("IMAGE_FAILED");
        return;
    }
    bool ok = printEpos(*payload);
    Serial.printf("http print %s\n", ok ? "ok" : "failed");
    server.send(ok ? 200 : 400, "text/xml", responseXml(ok, ok ? "" : (lastPrintCode.length() ? lastPrintCode : "EPOS_PARSE_ERROR")));
}

static void handleStatus()
{
    cors();
    server.send(200, "text/xml", responseXml(true, lastPrintCode));
}

static void handleDebugStatus()
{
    cors();
    String value = "code=";
    value += lastPrintCode;
    value += "\nrx=";
    value += String(config.rxPin);
    value += "\ntx=";
    value += String(config.txPin);
    value += "\nip=";
    value += deviceIp;
    value += "\nbuild=";
    value += firmwareBuild;
    value += "\n";
    value += lastRequestInfo;
    value += "\n";
    server.send(200, "text/plain", value);
}

void setupEposServer()
{
    Serial.println("http routes setup");
    const char *headers[] = {"Content-Length", "Content-Type"};
    server.collectHeaders(headers, 2);
    server.on("/cgi-bin/epos/service.cgi", HTTP_OPTIONS, handleOptions);
    server.on("/print", HTTP_OPTIONS, handleOptions);
    server.on("/status", HTTP_OPTIONS, handleOptions);
    server.on("/", HTTP_OPTIONS, handleOptions);
    server.on("/cgi-bin/epos/service.cgi", HTTP_GET, handleStatus);
    server.on("/print", HTTP_GET, handleStatus);
    server.on("/status", HTTP_GET, handleDebugStatus);
    server.on("/", HTTP_GET, handleStatus);
    server.on("/cgi-bin/epos/service.cgi", HTTP_POST, handlePrint, handleRawBody);
    server.on("/print", HTTP_POST, handlePrint, handleRawBody);
    server.on("/", HTTP_POST, handlePrint, handleRawBody);
    server.onNotFound([]()
                      {
        cors();
        server.send(404, "text/plain", "not found"); });
    server.begin();
}

void handleEposServer()
{
    server.handleClient();
}
