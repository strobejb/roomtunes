#pragma once

#include <QDomDocument>
#include <QMap>
#include <QString>
#include <QVector>

#include <initializer_list>
#include <string_view>
#include <utility>

namespace RoomTunes
{

inline QString xmlEscape(const QString &text)
{
    return text.toHtmlEscaped();
}

struct XmlOptions
{
    Qt::CaseSensitivity nameSensitivity = Qt::CaseSensitive;
};

class XmlNode
{
    friend class XmlDoc;

  public:
    XmlNode() = default;

    explicit XmlNode(QDomElement element, XmlOptions options = {}) : m_element(std::move(element)), m_options(options)
    {
    }

    explicit operator bool() const
    {
        return !m_element.isNull();
    }

    QString name() const
    {
        return elementName(m_element);
    }

    bool nameIs(std::string_view wanted) const
    {
        return nameIs(wanted, m_options.nameSensitivity);
    }

    bool nameIs(std::string_view wanted, Qt::CaseSensitivity sensitivity) const
    {
        return stringEquals(name(), wanted, sensitivity);
    }

    bool nameIn(std::initializer_list<std::string_view> names) const
    {
        return nameIn(names, m_options.nameSensitivity);
    }

    bool nameIn(std::initializer_list<std::string_view> names, Qt::CaseSensitivity sensitivity) const
    {
        for (std::string_view wanted : names)
            if (nameIs(wanted, sensitivity))
                return true;
        return false;
    }

    bool nameStartsWith(std::string_view prefix) const
    {
        return nameStartsWith(prefix, m_options.nameSensitivity);
    }

    bool nameStartsWith(std::string_view prefix, Qt::CaseSensitivity sensitivity) const
    {
        return name().startsWith(toQString(prefix), sensitivity);
    }

    QString text() const
    {
        return m_element.text();
    }

    QString text(std::string_view path) const
    {
        const XmlNode node = first(path);
        return node ? node.text() : QString();
    }

    bool textBool(std::string_view path, bool defaultValue = false) const
    {
        const QString value = text(path);
        if (value.isEmpty())
            return defaultValue;
        return value.compare(QStringLiteral("false"), Qt::CaseInsensitive) != 0 && value != QStringLiteral("0");
    }

    QString firstText(std::initializer_list<std::string_view> names) const
    {
        const XmlNode node = firstDescendant(names);
        return node ? node.text() : QString();
    }

    QString firstText(const QString &name) const
    {
        const XmlNode node = firstDescendantByName(name);
        return node ? node.text() : QString();
    }

    QString attr(std::string_view name) const
    {
        return attrByName(toQString(name));
    }

    int attrInt(std::string_view name, int defaultValue = 0) const
    {
        bool      ok    = false;
        const int value = attr(name).toInt(&ok);
        return ok ? value : defaultValue;
    }

    bool attrBool01(std::string_view name, bool defaultValue = false) const
    {
        const QString value = attr(name);
        if (value.isEmpty())
            return defaultValue;
        return value == QStringLiteral("1");
    }

    XmlNode child(std::string_view name) const
    {
        return childByName(toQString(name));
    }

    QVector<XmlNode> children(std::string_view name = {}) const
    {
        return childrenByName(toQString(name));
    }

    XmlNode first(std::string_view selector) const
    {
        const QVector<XmlNode> nodes = all(selector);
        return nodes.isEmpty() ? XmlNode() : nodes.first();
    }

    XmlNode firstDescendant(std::initializer_list<std::string_view> names) const
    {
        for (QDomElement child = m_element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
        {
            XmlNode node(child, m_options);
            if (node.nameIn(names))
                return node;

            node = node.firstDescendant(names);
            if (node)
                return node;
        }
        return {};
    }

    QVector<XmlNode> all(std::string_view selector) const
    {
        const QString path = toQString(selector);
        if (path.isEmpty())
            return {};

        if (path.startsWith(QStringLiteral(".//")))
            return descendantPath(path.mid(3));
        if (path.startsWith(QStringLiteral("//")))
            return descendantPath(path.mid(2));
        return childPath(path);
    }

    QMap<QString, QString> childTextMap() const
    {
        QMap<QString, QString> values;
        for (QDomElement child = m_element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
            values.insert(elementName(child), child.text());
        return values;
    }

  private:
    static QString toQString(std::string_view text)
    {
        return QString::fromUtf8(text.data(), qsizetype(text.size()));
    }

    static bool stringEquals(const QString &text, std::string_view wanted, Qt::CaseSensitivity sensitivity)
    {
        return text.compare(toQString(wanted), sensitivity) == 0;
    }

    static QString elementName(const QDomElement &element)
    {
        const QString localName = element.localName();
        return localName.isEmpty() ? element.tagName() : localName;
    }

    static bool matchesName(const QDomElement &element, const QString &wanted, Qt::CaseSensitivity sensitivity)
    {
        if (wanted == QStringLiteral("*"))
            return true;
        if (wanted.contains(QLatin1Char(':')))
            return element.tagName().compare(wanted, sensitivity) == 0;
        return elementName(element).compare(wanted, sensitivity) == 0;
    }

    XmlNode firstDescendantByName(const QString &name) const
    {
        for (QDomElement child = m_element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
        {
            XmlNode node(child, m_options);
            if (matchesName(child, name, m_options.nameSensitivity))
                return node;

            node = node.firstDescendantByName(name);
            if (node)
                return node;
        }
        return {};
    }

    QString attrByName(const QString &name) const
    {
        if (name.contains(QLatin1Char(':')))
            return m_element.attribute(name);
        if (m_element.hasAttribute(name))
            return m_element.attribute(name);

        const QDomNamedNodeMap attrs = m_element.attributes();
        for (int i = 0; i < attrs.size(); ++i)
        {
            const QDomNode attrNode = attrs.item(i);
            const QString  local    = attrNode.localName();
            const QString  tag      = attrNode.nodeName();
            if ((!local.isEmpty() && local == name) || tag == name)
                return attrNode.nodeValue();
        }
        return {};
    }

    XmlNode childByName(const QString &name) const
    {
        for (QDomElement child = m_element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
        {
            if (matchesName(child, name, m_options.nameSensitivity))
                return XmlNode(child, m_options);
        }
        return {};
    }

    QVector<XmlNode> childrenByName(const QString &name) const
    {
        QVector<XmlNode> nodes;
        for (QDomElement child = m_element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
        {
            if (name.isEmpty() || matchesName(child, name, m_options.nameSensitivity))
                nodes.append(XmlNode(child, m_options));
        }
        return nodes;
    }

    QVector<XmlNode> childPath(const QString &path) const
    {
        const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.isEmpty())
            return {};

        QVector<XmlNode> current{*this};
        for (const QString &part : parts)
        {
            QVector<XmlNode> next;
            for (const XmlNode &node : current)
            {
                for (const XmlNode &child : node.childrenByName(part))
                    next.append(child);
            }
            current = next;
            if (current.isEmpty())
                break;
        }
        return current;
    }

    QVector<XmlNode> descendantPath(const QString &path) const
    {
        const int     slash     = path.indexOf(QLatin1Char('/'));
        const QString firstPart = slash < 0 ? path : path.left(slash);
        const QString rest      = slash < 0 ? QString() : path.mid(slash + 1);

        QVector<XmlNode> matches;
        collectDescendants(m_element, firstPart, m_options, &matches);
        if (rest.isEmpty())
            return matches;

        QVector<XmlNode> result;
        for (const XmlNode &match : matches)
        {
            const QVector<XmlNode> nested = match.childPath(rest);
            for (const XmlNode &node : nested)
                result.append(node);
        }
        return result;
    }

    static void collectDescendants(const QDomElement &root, const QString &wanted, XmlOptions options,
                                   QVector<XmlNode> *matches)
    {
        for (QDomElement child = root.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
        {
            if (matchesName(child, wanted, options.nameSensitivity))
                matches->append(XmlNode(child, options));
            collectDescendants(child, wanted, options, matches);
        }
    }

  private:
    QDomElement m_element;
    XmlOptions  m_options;
};

class XmlDoc
{
  public:
    static XmlDoc parse(const QByteArray &body, XmlOptions options = {})
    {
        XmlDoc                          doc(options);
        const QDomDocument::ParseResult result =
            doc.m_document.setContent(body, QDomDocument::ParseOption::UseNamespaceProcessing);
        doc.m_valid        = bool(result);
        doc.m_errorMessage = result.errorMessage;
        doc.m_errorLine    = int(result.errorLine);
        doc.m_errorColumn  = int(result.errorColumn);
        return doc;
    }

    explicit XmlDoc(XmlOptions options = {}) : m_options(options)
    {
    }

    explicit operator bool() const
    {
        return m_valid;
    }

    QString errorMessage() const
    {
        return m_errorMessage;
    }

    int errorLine() const
    {
        return m_errorLine;
    }

    int errorColumn() const
    {
        return m_errorColumn;
    }

    XmlNode root() const
    {
        return XmlNode(m_document.documentElement(), m_options);
    }

    XmlNode first(std::string_view selector) const
    {
        const QVector<XmlNode> nodes = all(selector);
        return nodes.isEmpty() ? XmlNode() : nodes.first();
    }

    XmlNode firstDescendant(std::initializer_list<std::string_view> names) const
    {
        const XmlNode rootNode = root();
        if (!rootNode)
            return {};
        if (rootNode.nameIn(names))
            return rootNode;
        return rootNode.firstDescendant(names);
    }

    XmlNode firstDescendant(const QString &name) const
    {
        const XmlNode rootNode = root();
        if (!rootNode)
            return {};
        if (rootNode.name().compare(name, m_options.nameSensitivity) == 0)
            return rootNode;
        return rootNode.firstDescendantByName(name);
    }

    QString firstText(std::initializer_list<std::string_view> names) const
    {
        const XmlNode node = firstDescendant(names);
        return node ? node.text() : QString();
    }

    QString firstText(const QString &name) const
    {
        const XmlNode node = firstDescendant(name);
        return node ? node.text() : QString();
    }

    QVector<XmlNode> all(std::string_view selector) const
    {
        const XmlNode rootNode = root();
        if (!rootNode)
            return {};

        const QString    selectorText = QString::fromUtf8(selector.data(), qsizetype(selector.size()));
        QVector<XmlNode> nodes;
        if (selectorText.startsWith(QStringLiteral("//")))
        {
            const QString path      = selectorText.mid(2);
            const int     slash     = path.indexOf(QLatin1Char('/'));
            const QString firstPart = slash < 0 ? path : path.left(slash);
            const QString rest      = slash < 0 ? QString() : path.mid(slash + 1);
            if (firstPart == QStringLiteral("*") || rootNode.name().compare(firstPart, m_options.nameSensitivity) == 0)
            {
                if (rest.isEmpty())
                    nodes.append(rootNode);
                else
                {
                    const QVector<XmlNode> nested = rootNode.all(rest.toUtf8().constData());
                    for (const XmlNode &node : nested)
                        nodes.append(node);
                }
            }
        }
        else if (rootNode.name().compare(selectorText, m_options.nameSensitivity) == 0)
        {
            nodes.append(rootNode);
        }

        const QVector<XmlNode> nested = rootNode.all(selector);
        for (const XmlNode &node : nested)
            nodes.append(node);
        return nodes;
    }

  private:
    QDomDocument m_document;
    XmlOptions   m_options;
    bool         m_valid       = false;
    int          m_errorLine   = 0;
    int          m_errorColumn = 0;
    QString      m_errorMessage;
};

} // namespace RoomTunes
