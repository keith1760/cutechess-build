/*
    This file is part of Cute Chess (fork).

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef ENGINELISTSETTINGS_H
#define ENGINELISTSETTINGS_H

#include <QIODevice>
#include <QList>
#include <QString>
#include <QTextStream>
#include <QVariant>
#include <QVariantList>

#include <engineconfiguration.h>
#include <jsonparser.h>
#include <jsonserializer.h>

/*!
 * Converts lists of engine configurations to and from a JSON string, using
 * the same format as engines.json, so that a dialog can keep the complete
 * configuration of the engines it uses (options, init strings, ...) in
 * QSettings and not just their names.
 */
namespace EngineListSettings
{

inline QString toJson(const QList<EngineConfiguration>& engines)
{
	QVariantList list;
	for (const EngineConfiguration& config : engines)
		list << config.toVariant();

	QString text;
	QTextStream stream(&text);
	JsonSerializer serializer(list);
	serializer.serialize(stream);
	stream.flush();
	return text;
}

// Returns false (and an empty list) if the text is empty or not valid.
inline bool fromJson(const QString& text, QList<EngineConfiguration>* engines)
{
	engines->clear();
	if (text.trimmed().isEmpty())
		return false;

	QString copy(text);
	QTextStream stream(&copy, QIODevice::ReadOnly);
	JsonParser parser(stream);
	const QVariant parsed = parser.parse();
	if (parser.hasError() || parsed.typeId() != QMetaType::QVariantList)
		return false;

	const QVariantList list = parsed.toList();
	for (const QVariant& item : list)
		engines->append(EngineConfiguration(item));
	return true;
}

} // namespace EngineListSettings

#endif // ENGINELISTSETTINGS_H
