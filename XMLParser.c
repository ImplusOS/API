#include "XMLParser.h"
#include "Memory.h"
#include <string.h>

typedef struct {
    uint32_t node_count;
} xml_parse_context_t;

static void skip_whitespace(const char **s) {
    while (**s == ' ' || **s == '\t' || **s == '\r' || **s == '\n') {
        (*s)++;
    }
}

static bool parse_name(const char **s, char *out, uint32_t max_len) {
    uint32_t i = 0;
    while (**s && **s != ' ' && **s != '\t' && **s != '\r' && **s != '\n' &&
           **s != '=' && **s != '>' && **s != '/' && **s != '<') {
        if (i < max_len - 1) {
            out[i++] = **s;
        }
        (*s)++;
    }
    out[i] = '\0';
    return i > 0;
}

static bool parse_string(const char **s, char *out, uint32_t max_len) {
    char quote = **s;
    if (quote != '"' && quote != '\'') return false;
    (*s)++;
    
    uint32_t i = 0;
    while (**s && **s != quote) {
        if (i < max_len - 1) {
            out[i++] = **s;
        }
        (*s)++;
    }
    out[i] = '\0';
    if (**s == quote) (*s)++;
    return true;
}

static void skip_element(const char **s)
{
    if (!s || **s != '<') return;
    uint32_t depth = 0u;
    while (**s) {
        if (**s != '<') {
            (*s)++;
            continue;
        }

        if (*(*s + 1) == '?' || *(*s + 1) == '!') {
            while (**s && **s != '>') (*s)++;
            if (**s == '>') (*s)++;
            continue;
        }

        if (*(*s + 1) == '/') {
            while (**s && **s != '>') (*s)++;
            if (**s == '>') (*s)++;
            if (depth == 0u) return;
            --depth;
            if (depth == 0u) return;
            continue;
        }

        ++depth;
        (*s)++;
        char quote = '\0';
        while (**s) {
            if (quote != '\0') {
                if (**s == quote) quote = '\0';
                (*s)++;
                continue;
            }
            if (**s == '"' || **s == '\'') {
                quote = **s;
                (*s)++;
                continue;
            }
            if (**s == '/' && *(*s + 1) == '>') {
                (*s) += 2;
                if (depth > 0u) --depth;
                if (depth == 0u) return;
                break;
            }
            if (**s == '>') {
                (*s)++;
                break;
            }
            (*s)++;
        }
    }
}

static bool append_child(xml_node_t *node, xml_node_t *child)
{
    if (!node || !child || node->child_count >= XML_MAX_CHILDREN) return false;
    if (node->child_count == node->child_capacity) {
        uint32_t new_capacity = node->child_capacity == 0u ?
            8u : node->child_capacity * 2u;
        if (new_capacity > XML_MAX_CHILDREN) new_capacity = XML_MAX_CHILDREN;
        xml_node_t **children =
            (xml_node_t **)malloc((size_t)new_capacity * sizeof(*children));
        if (!children) return false;
        if (node->children) {
            memcpy(children, node->children,
                   (size_t)node->child_count * sizeof(*children));
            free(node->children);
        }
        node->children = children;
        node->child_capacity = new_capacity;
    }
    node->children[node->child_count++] = child;
    return true;
}

static xml_node_t *parse_node(const char **s, xml_parse_context_t *context,
                              uint32_t depth) {
    skip_whitespace(s);
    if (**s != '<') return NULL;
    if (!context || context->node_count >= XML_MAX_NODES ||
        depth >= XML_MAX_DEPTH) {
        skip_element(s);
        return NULL;
    }
    (*s)++;

    if (**s == '?' || **s == '!') {
        while (**s && **s != '>') (*s)++;
        if (**s == '>') (*s)++;
        return parse_node(s, context, depth);
    }
    
    if (**s == '/') {
        return NULL;
    }

    xml_node_t *node = (xml_node_t *)malloc(sizeof(xml_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(xml_node_t));
    context->node_count++;

    if (!parse_name(s, node->tag, sizeof(node->tag))) {
        free(node);
        return NULL;
    }

    while (1) {
        skip_whitespace(s);
        if (**s == '>' || **s == '/' || **s == '\0') break;

        char attr_name[XML_MAX_ATTR_NAME];
        if (!parse_name(s, attr_name, sizeof(attr_name))) break;

        skip_whitespace(s);
        if (**s == '=') {
            (*s)++;
            skip_whitespace(s);
            char attr_val[XML_MAX_ATTR_VALUE];
            if (parse_string(s, attr_val, sizeof(attr_val))) {
                if (node->attr_count < XML_MAX_ATTRIBUTES) {
                    os_strcpy_s(node->attributes[node->attr_count].name, XML_MAX_ATTR_NAME, attr_name);
                    os_strcpy_s(node->attributes[node->attr_count].value, XML_MAX_ATTR_VALUE, attr_val);
                    node->attr_count++;
                }
            }
        }
    }

    if (**s == '/') {
        (*s)++;
        skip_whitespace(s);
        if (**s == '>') (*s)++;
        return node;
    }

    if (**s == '>') {
        (*s)++;
        
        while (1) {
            skip_whitespace(s);
            if (**s == '\0') break;
            
            if (**s == '<') {
                if (*(*s + 1) == '/') {
                    (*s) += 2;
                    char end_tag[XML_MAX_TAG_LEN];
                    parse_name(s, end_tag, sizeof(end_tag));
                    skip_whitespace(s);
                    if (**s == '>') (*s)++;
                    break;
                } else {
                    xml_node_t *child = parse_node(s, context, depth + 1u);
                    if (child) {
                        if (!append_child(node, child)) {
                            xml_free(child);
                        }
                    }
                }
            } else {
                uint32_t i = 0;
                while (**s && **s != '<') {
                    if (i < sizeof(node->text) - 1) {
                        node->text[i++] = **s;
                    }
                    (*s)++;
                }
                node->text[i] = '\0';
                
                while(i > 0 && (node->text[i-1] == ' ' || node->text[i-1] == '\t' || node->text[i-1] == '\r' || node->text[i-1] == '\n')) {
                    i--;
                }
                node->text[i] = '\0';
            }
        }
    }

    return node;
}

xml_node_t *xml_parse(const char *xml_str) {
    if (!xml_str) return NULL;
    const char *s = xml_str;
    xml_parse_context_t context = {0u};
    return parse_node(&s, &context, 0u);
}

void xml_free(xml_node_t *node) {
    if (!node) return;
    for (uint32_t i = 0; i < node->child_count; i++) {
        xml_free(node->children[i]);
    }
    free(node->children);
    free(node);
}

const char *xml_get_attr(const xml_node_t *node, const char *name) {
    if (!node || !name) return NULL;
    for (uint32_t i = 0; i < node->attr_count; i++) {
        if (strcmp(node->attributes[i].name, name) == 0) {
            return node->attributes[i].value;
        }
    }
    return NULL;
}

xml_node_t *xml_find_child(const xml_node_t *node, const char *tag) {
    if (!node || !tag) return NULL;
    for (uint32_t i = 0; i < node->child_count; i++) {
        if (strcmp(node->children[i]->tag, tag) == 0) {
            return node->children[i];
        }
    }
    return NULL;
}
