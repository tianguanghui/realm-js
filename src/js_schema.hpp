////////////////////////////////////////////////////////////////////////////
//
// Copyright 2016 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#pragma once

#include <map>

#include "js_types.hpp"
#include "schema.hpp"

namespace realm {
namespace js {

template<typename T>
struct Schema {
    using TContext = typename T::Context;
    using TFunction = typename T::Function;
    using TObject = typename T::Object;
    using TValue = typename T::Value;
    using String = String<T>;
    using Object = Object<T>;
    using Value = Value<T>;

    using ObjectDefaults = std::map<std::string, Protected<TValue>>;
    using ObjectDefaultsMap = std::map<std::string, ObjectDefaults>;
    using ConstructorMap = std::map<std::string, Protected<TFunction>>;

    static TObject dict_for_property_array(TContext, const ObjectSchema &, TObject);
    static Property parse_property(TContext, TValue, std::string, ObjectDefaults &);
    static ObjectSchema parse_object_schema(TContext, TObject, ObjectDefaultsMap &, ConstructorMap &);
    static realm::Schema parse_schema(TContext, TObject, ObjectDefaultsMap &, ConstructorMap &);
};

template<typename T>
typename T::Object Schema<T>::dict_for_property_array(TContext ctx, const ObjectSchema &object_schema, TObject array) {
    size_t count = object_schema.properties.size();
    
    if (count != Object::validated_get_length(ctx, array)) {
        throw std::runtime_error("Array must contain values for all object properties");
    }

    TObject dict = Object::create_empty(ctx);

    for (uint32_t i = 0; i < count; i++) {
        TValue value = Object::get_property(ctx, array, i);
        Object::set_property(ctx, dict, object_schema.properties[i].name, value);
    }

    return dict;
}

template<typename T>
Property Schema<T>::parse_property(TContext ctx, TValue attributes, std::string property_name, ObjectDefaults &object_defaults) {
    static const String default_string = "default";
    static const String indexed_string = "indexed";
    static const String type_string = "type";
    static const String object_type_string = "objectType";
    static const String optional_string = "optional";
    
    Property prop;
    prop.name = property_name;
    
    TObject property_object = {};
    std::string type;
    
    if (Value::is_object(ctx, attributes)) {
        property_object = Value::validated_to_object(ctx, attributes);
        type = Object::validated_get_string(ctx, property_object, type_string);
        
        TValue optional_value = Object::get_property(ctx, property_object, optional_string);
        if (!Value::is_undefined(ctx, optional_value)) {
            prop.is_nullable = Value::validated_to_boolean(ctx, optional_value, "optional");
        }
    }
    else {
        type = Value::validated_to_string(ctx, attributes);
    }
    
    if (type == "bool") {
        prop.type = PropertyTypeBool;
    }
    else if (type == "int") {
        prop.type = PropertyTypeInt;
    }
    else if (type == "float") {
        prop.type = PropertyTypeFloat;
    }
    else if (type == "double") {
        prop.type = PropertyTypeDouble;
    }
    else if (type == "string") {
        prop.type = PropertyTypeString;
    }
    else if (type == "date") {
        prop.type = PropertyTypeDate;
    }
    else if (type == "data") {
        prop.type = PropertyTypeData;
    }
    else if (type == "list") {
        if (!Value::is_valid(property_object)) {
            throw std::runtime_error("List property must specify 'objectType'");
        }
        prop.type = PropertyTypeArray;
        prop.object_type = Object::validated_get_string(ctx, property_object, object_type_string);
    }
    else {
        prop.type = PropertyTypeObject;
        prop.is_nullable = true;
        
        // The type could either be 'object' or the name of another object type in the same schema.
        if (type == "object") {
            if (!Value::is_valid(property_object)) {
                throw std::runtime_error("Object property must specify 'objectType'");
            }
            prop.object_type = Object::validated_get_string(ctx, property_object, object_type_string);
        }
        else {
            prop.object_type = type;
        }
    }
    
    if (Value::is_valid(property_object)) {
        TValue default_value = Object::get_property(ctx, property_object, default_string);
        if (!Value::is_undefined(ctx, default_value)) {
            object_defaults.emplace(prop.name, Protected<TValue>(ctx, default_value));
        }
        
        TValue indexed_value = Object::get_property(ctx, property_object, indexed_string);
        if (!Value::is_undefined(ctx, indexed_value)) {
            prop.is_indexed = Value::validated_to_boolean(ctx, indexed_value);
        }
    }
    
    return prop;
}

template<typename T>
ObjectSchema Schema<T>::parse_object_schema(TContext ctx, TObject object_schema_object, ObjectDefaultsMap &defaults, ConstructorMap &constructors) {
    static const String name_string = "name";
    static const String primary_string = "primaryKey";
    static const String properties_string = "properties";
    static const String schema_string = "schema";
    
    TFunction object_constructor = {};
    if (Value::is_constructor(ctx, object_schema_object)) {
        object_constructor = Value::to_constructor(ctx, object_schema_object);
        object_schema_object = Object::validated_get_object(ctx, object_constructor, schema_string, "Realm object constructor must have a 'schema' property.");
    }
    
    ObjectDefaults object_defaults;
    ObjectSchema object_schema;
    object_schema.name = Object::validated_get_string(ctx, object_schema_object, name_string);
    
    TObject properties_object = Object::validated_get_object(ctx, object_schema_object, properties_string, "ObjectSchema must have a 'properties' object.");
    if (Value::is_array(ctx, properties_object)) {
        uint32_t length = Object::validated_get_length(ctx, properties_object);
        for (uint32_t i = 0; i < length; i++) {
            TObject property_object = Object::validated_get_object(ctx, properties_object, i);
            std::string property_name = Object::validated_get_string(ctx, property_object, name_string);
            object_schema.properties.emplace_back(parse_property(ctx, property_object, property_name, object_defaults));
        }
    }
    else {
        auto property_names = Object::get_property_names(ctx, properties_object);
        for (auto &property_name : property_names) {
            TValue property_value = Object::get_property(ctx, properties_object, property_name);
            object_schema.properties.emplace_back(parse_property(ctx, property_value, property_name, object_defaults));
        }
    }

    TValue primary_value = Object::get_property(ctx, object_schema_object, primary_string);
    if (!Value::is_undefined(ctx, primary_value)) {
        object_schema.primary_key = Value::validated_to_string(ctx, primary_value);
        Property *property = object_schema.primary_key_property();
        if (!property) {
            throw std::runtime_error("Missing primary key property '" + object_schema.primary_key + "'");
        }
        property->is_primary = true;
    }
    
    // Store prototype so that objects of this type will have their prototype set to this prototype object.
    if (Value::is_valid(object_constructor)) {
        constructors.emplace(object_schema.name, Protected<TFunction>(ctx, object_constructor));
    }
    
    defaults.emplace(object_schema.name, std::move(object_defaults));
    
    return object_schema;
}
    
template<typename T>
realm::Schema Schema<T>::parse_schema(TContext ctx, TObject schema_object, ObjectDefaultsMap &defaults, ConstructorMap &constructors) {
    std::vector<ObjectSchema> schema;
    uint32_t length = Object::validated_get_length(ctx, schema_object);

    for (uint32_t i = 0; i < length; i++) {
        TObject object_schema_object = Object::validated_get_object(ctx, schema_object, i);
        ObjectSchema object_schema = parse_object_schema(ctx, object_schema_object, defaults, constructors);
        schema.emplace_back(std::move(object_schema));
    }

    return realm::Schema(schema);
}

} // js
} // realm
