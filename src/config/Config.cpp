#include "config/Config.hpp"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlschemas.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace audiocompd {
namespace {

using XmlDocPtr = std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>;
using SchemaParserPtr = std::unique_ptr<xmlSchemaParserCtxt, decltype(&xmlSchemaFreeParserCtxt)>;
using SchemaPtr = std::unique_ptr<xmlSchema, decltype(&xmlSchemaFree)>;
using SchemaValidationPtr = std::unique_ptr<xmlSchemaValidCtxt, decltype(&xmlSchemaFreeValidCtxt)>;

struct XmlCharDeleter {
    void operator()(xmlChar* value) const noexcept {
        xmlFree(value);
    }
};

using XmlCharPtr = std::unique_ptr<xmlChar, XmlCharDeleter>;

bool isElement(const xmlNode* node, const char* name) {
    return node != nullptr && node->type == XML_ELEMENT_NODE &&
           xmlStrEqual(node->name, BAD_CAST name) != 0;
}

xmlNode* findChild(xmlNode* parent, const char* name, bool required = true) {
    for (xmlNode* child = parent != nullptr ? parent->children : nullptr;
         child != nullptr;
         child = child->next) {
        if (isElement(child, name)) {
            return child;
        }
    }

    if (required) {
        throw std::runtime_error("Missing XML element <" + std::string(name) + ">");
    }
    return nullptr;
}

std::string nodeText(xmlNode* node) {
    XmlCharPtr value(xmlNodeGetContent(node));
    if (!value) {
        return {};
    }
    return reinterpret_cast<const char*>(value.get());
}

std::string textOf(xmlNode* parent, const char* name, bool required = true) {
    xmlNode* node = findChild(parent, name, required);
    return node == nullptr ? std::string{} : nodeText(node);
}

std::size_t parseSize(const std::string& value, const char* name) {
    std::size_t parsedCharacters = 0;
    const auto result = std::stoull(value, &parsedCharacters);
    if (parsedCharacters != value.size()) {
        throw std::runtime_error("Invalid integer value for " + std::string(name));
    }
    return static_cast<std::size_t>(result);
}

float parseFloat(const std::string& value, const char* name) {
    std::size_t parsedCharacters = 0;
    const float result = std::stof(value, &parsedCharacters);
    if (parsedCharacters != value.size()) {
        throw std::runtime_error("Invalid floating-point value for " + std::string(name));
    }
    return result;
}

bool parseBool(std::string value, const char* name) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    if (value == "true" || value == "1") {
        return true;
    }
    if (value == "false" || value == "0") {
        return false;
    }
    throw std::runtime_error("Invalid boolean value for " + std::string(name));
}

std::vector<std::string> collectText(xmlNode* parent, const char* name) {
    std::vector<std::string> values;
    for (xmlNode* child = parent != nullptr ? parent->children : nullptr;
         child != nullptr;
         child = child->next) {
        if (isElement(child, name)) {
            values.push_back(nodeText(child));
        }
    }
    return values;
}

void validateDocument(xmlDoc* document, const std::string& schemaPath) {
    SchemaParserPtr parser(xmlSchemaNewParserCtxt(schemaPath.c_str()), &xmlSchemaFreeParserCtxt);
    if (!parser) {
        throw std::runtime_error("Cannot create XSD parser for " + schemaPath);
    }

    SchemaPtr schema(xmlSchemaParse(parser.get()), &xmlSchemaFree);
    if (!schema) {
        throw std::runtime_error("Cannot parse XSD schema " + schemaPath);
    }

    SchemaValidationPtr validation(xmlSchemaNewValidCtxt(schema.get()), &xmlSchemaFreeValidCtxt);
    if (!validation) {
        throw std::runtime_error("Cannot create XSD validation context");
    }

    const int validationResult = xmlSchemaValidateDoc(validation.get(), document);
    if (validationResult != 0) {
        throw std::runtime_error(validationResult > 0
                                     ? "Configuration does not match the XSD schema"
                                     : "Configuration validation failed internally");
    }
}

BackendConfig parseBackend(xmlNode* audioNode) {
    xmlNode* backendNode = findChild(audioNode, "backend");
    xmlNode* selected = nullptr;
    for (xmlNode* child = backendNode->children; child != nullptr; child = child->next) {
        if (child->type == XML_ELEMENT_NODE) {
            selected = child;
            break;
        }
    }

    if (isElement(selected, "alsa")) {
        AlsaConfig config;
        config.inputDevice = textOf(selected, "inputDevice");
        config.outputDevice = textOf(selected, "outputDevice");
        config.sampleRate = static_cast<std::uint32_t>(
            parseSize(textOf(selected, "sampleRate"), "sampleRate"));
        config.channels = parseSize(textOf(selected, "channels"), "channels");
        config.periodFrames = parseSize(textOf(selected, "periodFrames"), "periodFrames");
        config.periods = parseSize(textOf(selected, "periods"), "periods");
        return config;
    }

    if (isElement(selected, "jack")) {
        JackConfig config;
        config.clientName = textOf(selected, "clientName");
        config.channels = parseSize(textOf(selected, "channels"), "channels");
        config.autoConnect = parseBool(textOf(selected, "autoConnect"), "autoConnect");
        config.inputPorts = collectText(selected, "inputPort");
        config.outputPorts = collectText(selected, "outputPort");
        return config;
    }

    if (isElement(selected, "pipewire")) {
        PipeWireConfig config;
        config.nodeName = textOf(selected, "nodeName");
        config.nodeDescription = textOf(selected, "nodeDescription");
        config.targetSink = textOf(selected, "targetSink");
        config.sampleRate = static_cast<std::uint32_t>(
            parseSize(textOf(selected, "sampleRate"), "sampleRate"));
        config.channels = parseSize(textOf(selected, "channels"), "channels");
        config.quantum = parseSize(textOf(selected, "quantum"), "quantum");
        return config;
    }

    throw std::runtime_error("No supported audio backend is configured");
}

CompressorConfig parseCompressor(xmlNode* root) {
    xmlNode* node = findChild(root, "compressor");
    CompressorConfig config;

    XmlCharPtr enabled(xmlGetProp(node, BAD_CAST "enabled"));
    if (!enabled) {
        throw std::runtime_error("Missing compressor enabled attribute");
    }

    config.enabled = parseBool(reinterpret_cast<const char*>(enabled.get()), "compressor.enabled");
    config.thresholdDb = parseFloat(textOf(node, "thresholdDb"), "thresholdDb");
    config.ratio = parseFloat(textOf(node, "ratio"), "ratio");
    config.attackMs = parseFloat(textOf(node, "attackMs"), "attackMs");
    config.releaseMs = parseFloat(textOf(node, "releaseMs"), "releaseMs");
    config.kneeDb = parseFloat(textOf(node, "kneeDb"), "kneeDb");
    config.makeupGainDb = parseFloat(textOf(node, "makeupGainDb"), "makeupGainDb");
    return config;
}

LoggingConfig parseLogging(xmlNode* root) {
    xmlNode* node = findChild(root, "logging");
    LoggingConfig config;
    config.level = textOf(node, "level");
    config.filePath = textOf(node, "filePath", false);
    return config;
}

} // namespace

Config::Config(AppConfig values) : values_(std::move(values)) {}

Config Config::load(const std::string& xmlPath, const std::string& schemaPath) {
    static std::once_flag libxmlInitialization;
    std::call_once(libxmlInitialization, [] { xmlInitParser(); });

    XmlDocPtr document(xmlReadFile(xmlPath.c_str(), nullptr, XML_PARSE_NONET), &xmlFreeDoc);
    if (!document) {
        throw std::runtime_error("Cannot parse configuration file " + xmlPath);
    }

    validateDocument(document.get(), schemaPath);

    xmlNode* root = xmlDocGetRootElement(document.get());
    if (!isElement(root, "audiocompd")) {
        throw std::runtime_error("The configuration root must be <audiocompd>");
    }

    AppConfig config;
    config.backend = parseBackend(findChild(root, "audio"));
    config.compressor = parseCompressor(root);
    config.logging = parseLogging(root);
    return Config(std::move(config));
}

const AppConfig& Config::values() const noexcept {
    return values_;
}

} // namespace audiocompd

