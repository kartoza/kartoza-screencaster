#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include "recorder/recorder.h" // for RecordingOptions

namespace Merger {

// Shared helpers
QString titleSlug(const QString &title);
QString outputFileName(int number, const QString &title, const QString &suffix = {});
void appendLogoInputArgs(QStringList &args, const RecordingOptions::LogoOpts &logo);

// FFmpeg utilities
qint64 getVideoDurationUs(const QString &filePath);
QString concatenateParts(const QStringList &parts, const QString &outputFile,
                         const QString &listPrefix = "concat");

// Audio processing
struct LoudnormParams {
    bool valid = false;
    QString filterString;
};
LoudnormParams analyzeAudio(const QString &audioFile);
bool normalizeAudio(const QString &inputFile, const QString &outputFile,
                    const LoudnormParams &params);

// FFmpeg progress runner — emits progress via callback
using ProgressCallback = std::function<void(int percent)>;
int runFFmpegWithProgress(const QStringList &args, qint64 durationUs,
                          const ProgressCallback &onProgress = nullptr);

// Filter building for merged (landscape) video
struct MergeInputs {
    QString screenFile;
    QString audioFile;
    QString webcamFile;
    RecordingOptions opts;
};
QStringList buildMergedArgs(const MergeInputs &in, const QString &outputFile);

// Filter building for vertical (9:16) video
QStringList buildVerticalArgs(const MergeInputs &in, const QString &outputFile);

} // namespace Merger
