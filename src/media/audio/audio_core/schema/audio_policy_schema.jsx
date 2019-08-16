{
  "definitions": {
    // behaviors, render_usage, and capture_usage should be enum's, but rapidjson
    // does not validate against the enum, which means there's not 'breaking' reason
    // that a person would know to update the schema.
    "behaviors": { "type": "string" },
    "render_usage": { "type": "string" },
    "capture_usage": { "type": "string" },
    "rule": {
      "active": {
        "oneOf": [
          {"$ref": "#/definitions/render_usage"},
          {"$ref": "#/definitions/capture_usage"}
        ]
       },
       "affected": {
        "oneOf": [
          {"$ref": "#/definitions/render_usage"},
          {"$ref": "#/definitions/capture_usage"}
        ]
       },
       "behavior": {"$ref": "#/definitions/behaviors"}
    }
   },

  "type": "object",
  "properties": {
    "audio_policy_rules": {
      "type": "array",
      "items": { "$ref": "#/definitions/rule" }
    }
  },
  "required": ["audio_policy_rules"],
  "additionalProperties" : true
}