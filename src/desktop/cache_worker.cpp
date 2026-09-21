#include "decode/decoder.hpp"
#include "project/persistence.hpp"
#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <iostream>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() != 2)
            throw nle::DomainError("Expected a derived-media request");
        QFile file(args[1]);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
            throw nle::DomainError("Invalid cache request");
        const auto request = QJsonDocument::fromJson(file.readAll()).object();
        const auto project = nle::deserialize(request["project"].toString().toStdString());
        if (project.media.size() != 1)
            throw nle::DomainError("Expected one media asset");
        const auto &asset = project.media.front();
        const nle::RationalTime time{request["value"].toString().toLongLong(),
                                     request["rate"].toString().toLongLong()};
        if (time >= asset.duration || time > nle::RationalTime{86400})
            throw nle::DomainError("Cache time is outside source");
        const auto stream = request["stream"].toInt(-1);
        if (stream < 0)
            throw nle::DomainError("Invalid stream");
        std::atomic_bool stop = false;
        nle::decode::Decoder decoder(asset, static_cast<std::uint32_t>(stream), 160, 90, stop);
        decoder.seek(time);
        QJsonObject result;
        if (request["wave"].toBool()) {
            const auto start = nle::decode::sample_floor(time);
            const auto finish =
                std::min(start + 10 * 48000, nle::decode::sample_ceil(asset.duration));
            QJsonArray peaks;
            for (auto at = start; at < finish; at += 480) {
                std::vector<float> samples(
                    static_cast<std::size_t>(std::min<std::int64_t>(480, finish - at)) * 2);
                decoder.mix(at, samples, 1);
                float peak = 0;
                for (const auto value : samples) {
                    if (!std::isfinite(value))
                        throw nle::DomainError("Invalid audio sample");
                    peak = std::max(peak, std::abs(value));
                }
                peaks.push_back(std::min(1.0F, peak));
            }
            result["peaks"] = peaks;
        } else {
            const auto frame = decoder.video(time);
            QImage image(160, 90, QImage::Format_RGB888);
            if (frame.rgb.empty())
                image.fill(Qt::black);
            else
                image = QImage(frame.rgb.data(), 160, 90, 160 * 3, QImage::Format_RGB888).copy();
            QByteArray jpeg;
            QBuffer buffer(&jpeg);
            buffer.open(QIODevice::WriteOnly);
            if (!image.save(&buffer, "JPG", 80))
                throw nle::DomainError("Thumbnail encoding failed");
            result["image"] = QString::fromLatin1(jpeg.toBase64());
        }
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString();
        return 0;
    } catch (const std::exception &e) {
        std::cout << QJsonDocument(QJsonObject{{"error", QString::fromUtf8(e.what()).left(1024)}})
                         .toJson(QJsonDocument::Compact)
                         .toStdString();
        return 1;
    }
}
