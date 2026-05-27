#pragma once

#include <QString>

class Addr2LineResolver
{
public:
    explicit Addr2LineResolver(const QString &elfPath);

    bool isValid() const;

    QString resolve(quint32 addr) const;

private:
    QString findToolPath() const;

    QString m_elfPath;
    QString m_toolPath;
};
