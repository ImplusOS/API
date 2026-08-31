#pragma once

#include <stdint.h>
#include <stdbool.h>

#define XML_MAX_TAG_LEN        64
#define XML_MAX_ATTR_NAME      32
#define XML_MAX_ATTR_VALUE     128
#define XML_MAX_CHILDREN       256
#define XML_MAX_NODES          2048
#define XML_MAX_DEPTH          64
#define XML_MAX_ATTRIBUTES     8

typedef struct xml_attribute {
    char name[XML_MAX_ATTR_NAME];
    char value[XML_MAX_ATTR_VALUE];
} xml_attribute_t;

typedef struct xml_node {
    char tag[XML_MAX_TAG_LEN];
    xml_attribute_t attributes[XML_MAX_ATTRIBUTES];
    uint32_t attr_count;
    char text[XML_MAX_ATTR_VALUE];
    struct xml_node **children;
    uint32_t child_count;
    uint32_t child_capacity;
} xml_node_t;

xml_node_t *xml_parse(const char *xml_str);
void xml_free(xml_node_t *node);
const char *xml_get_attr(const xml_node_t *node, const char *name);
xml_node_t *xml_find_child(const xml_node_t *node, const char *tag);
