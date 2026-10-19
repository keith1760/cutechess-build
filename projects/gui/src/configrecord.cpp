/*
    This file is part of Cute Chess (fork).

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "configrecord.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QSaveFile>
#include <QSettings>
#include <QStringList>

#include <enginemanager.h>
#include <engineconfiguration.h>

namespace {

const char* FormatName = "cutechess-configuration";

// Converts a QSettings value to JSON. Returns false for values that
// cannot be represented. Binary values become {"base64": "..."}.
bool toJson(const QVariant& value, QJsonValue* out)
{
	switch (value.typeId())
	{
	case QMetaType::Bool:
	case QMetaType::Int:
	case QMetaType::UInt:
	case QMetaType::LongLong:
	case QMetaType::ULongLong:
	case QMetaType::Double:
		*out = QJsonValue::fromVariant(value);
		return true;
	case QMetaType::QStringList:
		*out = QJsonArray::fromStringList(value.toStringList());
		return true;
	case QMetaType::QByteArray:
	{
		QJsonObject bytes;
		bytes.insert("base64", QString::fromLatin1(value.toByteArray().toBase64()));
		*out = bytes;
		return true;
	}
	default:
		if (!value.canConvert<QString>())
			return false;
		*out = value.toString();
		return true;
	}
}

QVariant fromJson(const QJsonValue& value)
{
	if (value.isObject() && value.toObject().contains("base64"))
		return QByteArray::fromBase64(
			value.toObject().value("base64").toString().toLatin1());
	if (value.isArray())
	{
		QStringList list;
		const QJsonArray array = value.toArray();
		for (const QJsonValue& item : array)
			list << item.toVariant().toString();
		return list;
	}
	return value.toVariant();
}

} // namespace

const int ConfigRecord::FormatVersion = 1;

bool ConfigRecord::isExcluded(const QString& key)
{
	return key == QLatin1String("ui/last_config_dir");
}

bool ConfigRecord::isWindowLayout(const QString& key)
{
	return key.startsWith(QLatin1String("ui/mainwindow/"));
}

QString ConfigRecord::suggestedFileName()
{
	return QString("CutechessConfig-%1.json")
		.arg(QDate::currentDate().toString(Qt::ISODate));
}

bool ConfigRecord::save(const QString& fileName,
			QSettings& settings,
			EngineManager* engines,
			QString* error,
			const QVariantMap& windowLayout)
{
	settings.sync();

	QJsonObject settingsObject;
	const QStringList keys = settings.allKeys();
	for (const QString& key : keys)
	{
		if (isExcluded(key))
			continue;
		// Stale on-disk layout is replaced by the caller's live one
		if (!windowLayout.isEmpty() && isWindowLayout(key))
			continue;

		QJsonValue json;
		if (toJson(settings.value(key), &json))
			settingsObject.insert(key, json);
	}

	for (auto it = windowLayout.constBegin(); it != windowLayout.constEnd(); ++it)
	{
		QJsonValue json;
		if (isWindowLayout(it.key()) && toJson(it.value(), &json))
			settingsObject.insert(it.key(), json);
	}

	QJsonObject root;
	root.insert("format", QString(FormatName));
	root.insert("version", FormatVersion);
	root.insert("savedAt", QDateTime::currentDateTime().toString(Qt::ISODate));
	root.insert("settings", settingsObject);

	if (engines != nullptr)
	{
		QJsonArray engineArray;
		const auto list = engines->engines();
		for (const EngineConfiguration& config : list)
			engineArray.append(QJsonValue::fromVariant(config.toVariant()));
		root.insert("engines", engineArray);
	}

	QSaveFile file(fileName);
	if (!file.open(QIODevice::WriteOnly))
	{
		if (error)
			*error = QString("Cannot write \"%1\": %2")
				 .arg(fileName, file.errorString());
		return false;
	}
	file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	if (!file.commit())
	{
		if (error)
			*error = QString("Cannot write \"%1\": %2")
				 .arg(fileName, file.errorString());
		return false;
	}
	return true;
}

bool ConfigRecord::load(const QString& fileName,
			Contents* contents,
			QString* error)
{
	QFile file(fileName);
	if (!file.open(QIODevice::ReadOnly))
	{
		if (error)
			*error = QString("Cannot read \"%1\": %2")
				 .arg(fileName, file.errorString());
		return false;
	}

	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (doc.isNull() || !doc.isObject())
	{
		if (error)
			*error = QString("\"%1\" is not a valid configuration file (%2).")
				 .arg(fileName, parseError.errorString());
		return false;
	}

	const QJsonObject root = doc.object();
	if (root.value("format").toString() != QLatin1String(FormatName)
	||  !root.value("settings").isObject())
	{
		if (error)
			*error = QString("\"%1\" is not a Cute Chess configuration file.")
				 .arg(fileName);
		return false;
	}

	const int version = root.value("version").toInt(0);
	if (version < 1 || version > FormatVersion)
	{
		if (error)
			*error = QString("This configuration file has format version %1, "
					 "but this program only understands versions up to %2.")
				 .arg(version).arg(FormatVersion);
		return false;
	}

	Contents result;
	result.version = version;
	result.savedAt = root.value("savedAt").toString();

	const QJsonObject settingsObject = root.value("settings").toObject();
	for (auto it = settingsObject.constBegin(); it != settingsObject.constEnd(); ++it)
	{
		const bool isBytes = it.value().isObject()
			&& it.value().toObject().contains("base64");
		if (isExcluded(it.key()) || it.value().isNull()
		|| (it.value().isObject() && !isBytes))
			continue;
		result.settings.insert(it.key(), fromJson(it.value()));
		if (isWindowLayout(it.key()))
			result.hasWindowLayout = true;
	}

	if (root.contains("engines"))
	{
		if (!root.value("engines").isArray())
		{
			if (error)
				*error = "The engine list in the configuration file is damaged.";
			return false;
		}
		const QJsonArray engineArray = root.value("engines").toArray();
		for (const QJsonValue& engine : engineArray)
		{
			if (!engine.isObject())
			{
				if (error)
					*error = "The engine list in the configuration file is damaged.";
				return false;
			}
			result.engines << engine.toVariant();
		}
		result.hasEngines = true;
	}

	*contents = result;
	return true;
}

bool ConfigRecord::apply(const Contents& contents,
			 QSettings& settings,
			 EngineManager* engines,
			 const QString& enginesFile,
			 QString* error)
{
	Q_UNUSED(error);

	// Anything the record does not mention goes back to its default.
	const QStringList oldKeys = settings.allKeys();
	for (const QString& key : oldKeys)
	{
		if (isExcluded(key))
			continue;
		// A record without a window layout leaves the current one alone
		if (isWindowLayout(key) && !contents.hasWindowLayout)
			continue;
		settings.remove(key);
	}

	for (auto it = contents.settings.constBegin();
	     it != contents.settings.constEnd(); ++it)
	{
		if (!isExcluded(it.key()))
			settings.setValue(it.key(), it.value());
	}
	settings.sync();

	if (contents.hasEngines && engines != nullptr)
	{
		QList<EngineConfiguration> list;
		for (const QVariant& engine : contents.engines)
			list << EngineConfiguration(engine);
		engines->setEngines(list);
		engines->saveEngines(enginesFile);
	}

	return true;
}
