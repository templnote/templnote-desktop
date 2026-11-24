#pragma once

// Minimal stand-in for the desktop AgentService used by the prompt-panel QML
// review smoke test. It exposes the same properties and invokables so binding
// and delegate errors in the real decoration are caught by CTest.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class FakeAgent : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
  Q_PROPERTY(QString currentAgent READ currentAgent WRITE setCurrentAgent
                 NOTIFY agentChanged)
  Q_PROPERTY(QString lastError READ lastError WRITE setLastError NOTIFY
                 lastErrorChanged)
  Q_PROPERTY(QString connectionError READ connectionError WRITE
                 setConnectionError NOTIFY connectionErrorChanged)
  Q_PROPERTY(bool hasPending READ hasPending WRITE setHasPending
                 NOTIFY pendingChanged)
  Q_PROPERTY(QVariantList agentRuns READ agentRuns WRITE setAgentRuns NOTIFY
                 agentRunsChanged)
  Q_PROPERTY(QVariantList pendingDiff READ pendingDiff WRITE setPendingDiff
                 NOTIFY pendingChanged)
  Q_PROPERTY(QVariantList answers READ answers WRITE setAnswers NOTIFY
                 answerChanged)
  Q_PROPERTY(QString lastAnswer READ lastAnswer WRITE setLastAnswer NOTIFY
                 answerChanged)

  Q_PROPERTY(QVariantList steps READ steps WRITE setSteps NOTIFY stepsChanged)
  Q_PROPERTY(QVariantList transcriptModel READ transcript NOTIFY transcriptChanged)
  Q_PROPERTY(QVariantList stepsModel READ steps NOTIFY stepsChanged)
  Q_PROPERTY(QVariantList sessionsModel READ sessions NOTIFY sessionsChanged)
  Q_PROPERTY(QVariantList subagentModel READ subagents NOTIFY sessionsChanged)
  Q_PROPERTY(QVariantList permissionModel READ permissions NOTIFY permissionChanged)
  Q_PROPERTY(QVariantList skillModel READ skills NOTIFY skillsChanged)
  Q_PROPERTY(bool compacted READ compacted NOTIFY compactedChanged)
  Q_PROPERTY(int liveTokens READ liveTokens NOTIFY answerChanged)
  Q_PROPERTY(double liveCostUsd READ liveCostUsd NOTIFY answerChanged)
  Q_PROPERTY(QString activeSessionId READ activeSessionId NOTIFY sessionsChanged)
  Q_PROPERTY(QString activeSessionTitle READ activeSessionTitle NOTIFY sessionsChanged)
  Q_PROPERTY(QVariantList transcript READ transcript WRITE setTranscript NOTIFY
                  transcriptChanged)

public:
  explicit FakeAgent(QObject *parent = nullptr) : QObject(parent) {}

  bool running() const { return m_running; }
  QVariantList sessions() const { return m_sessions; }
  QVariantList subagents() const { return m_subagents; }
  QVariantList permissions() const { return m_permissions; }
  QVariantList skills() const { return m_skills; }
  bool compacted() const { return m_compacted; }
  int liveTokens() const { return m_liveTokens; }
  double liveCostUsd() const { return 0.012; }
  QString activeSessionId() const { return QStringLiteral("session-1"); }
  QString activeSessionTitle() const { return QStringLiteral("Test Chat"); }
  int steerCount() const { return m_steerCount; }
  int approvalCount() const { return m_approvalCount; }
  int skillLoads() const { return m_skillLoads; }
  int acceptAllCount() const { return m_acceptAllCount; }
  int rejectAllCount() const { return m_rejectAllCount; }
  int switchCount() const { return m_switchCount; }
  int createSessionCount() const { return m_createSessionCount; }
  QString lastCreatedTitle() const { return m_lastCreatedTitle; }
  void setRunning(bool value) {
    if (m_running == value)
      return;
    m_running = value;
    emit runningChanged();
  }

  QString currentAgent() const { return m_currentAgent; }
  void setCurrentAgent(const QString &value) {
    if (m_currentAgent == value)
      return;
    m_currentAgent = value;
    emit agentChanged();
  }

  QString lastError() const { return m_lastError; }
  void setLastError(const QString &value) {
    if (m_lastError == value)
      return;
    m_lastError = value;
    emit lastErrorChanged();
  }

  QString connectionError() const { return m_connectionError; }
  void setConnectionError(const QString &value) {
    if (m_connectionError == value)
      return;
    m_connectionError = value;
    emit connectionErrorChanged();
  }

  bool hasPending() const { return m_pending; }
  void setHasPending(bool value) {
    if (m_pending == value)
      return;
    m_pending = value;
    emit pendingChanged();
  }

  QVariantList agentRuns() const { return m_agentRuns; }
  void setAgentRuns(const QVariantList &value) {
    m_agentRuns = value;
    emit agentRunsChanged();
  }

  QVariantList pendingDiff() const { return m_diff; }
  void setPendingDiff(const QVariantList &value) {
    if (m_diff == value)
      return;
    m_diff = value;
    emit pendingChanged();
  }

  QVariantList answers() const { return m_answers; }
  void setAnswers(const QVariantList &value) {
    m_answers = value;
    emit answerChanged();
  }
  QString lastAnswer() const { return m_lastAnswer; }
  void setLastAnswer(const QString &value) {
    m_lastAnswer = value;
    emit answerChanged();
  }

  QVariantList steps() const { return m_steps; }
  void setSteps(const QVariantList &value) {
    m_steps = value;
    emit stepsChanged();
  }
  QVariantList transcript() const { return m_transcript; }
  void setSessions(const QVariantList &value) { m_sessions = value; emit sessionsChanged(); }
  void setSubagents(const QVariantList &value) { m_subagents = value; emit sessionsChanged(); }
  void setPermissions(const QVariantList &value) { m_permissions = value; emit permissionChanged(); }
  void setSkills(const QVariantList &value) { m_skills = value; emit skillsChanged(); }
  void setCompacted(bool value) { m_compacted = value; emit compactedChanged(); }
  void setTranscript(const QVariantList &value) {
    m_transcript = value;
    emit transcriptChanged();
  }

  int commitCount() const { return m_commits; }
  int rejectCount() const { return m_rejects; }
  int promptCount() const { return m_prompts; }
  int regenerateCount() const { return m_regenerates; }
  int positiveFeedback() const { return m_positiveFeedback; }
  int negativeFeedback() const { return m_negativeFeedback; }

  Q_INVOKABLE bool commit() {
    ++m_commits;
    return true;
  }
  Q_INVOKABLE void reject() { ++m_rejects; }
  Q_INVOKABLE void clearErrors() {
    setLastError({});
    setConnectionError({});
  }
  Q_INVOKABLE void cancel() {}
  Q_INVOKABLE void clearTranscript() {
    m_transcript.clear();
    emit transcriptChanged();
  }
  Q_INVOKABLE void regenerate() { ++m_regenerates; }
  Q_INVOKABLE void sendFeedback(bool positive) {
    positive ? ++m_positiveFeedback : ++m_negativeFeedback;
  }
  Q_INVOKABLE void sendPrompt(const QString &text) {
    ++m_prompts;
    QVariantMap turn;
    turn.insert(QStringLiteral("role"), QStringLiteral("user"));
    turn.insert(QStringLiteral("text"), text);
    turn.insert(QStringLiteral("time"), QStringLiteral("12:19 PM"));
    m_transcript.append(turn);
    emit transcriptChanged();
    setRunning(true);
    emit promptStarted(text);
  }
  Q_INVOKABLE void finish() {
    setRunning(false);
    emit finishedChanged();
  }
  Q_INVOKABLE void setAgentAccepted(int index, bool accepted) {
    if (index < 0 || index >= m_agentRuns.size())
      return;
    QVariantMap run = m_agentRuns.at(index).toMap();
    run.insert(QStringLiteral("accepted"), accepted);
    m_agentRuns.replace(index, run);
    emit agentRunsChanged();
  }

  Q_INVOKABLE bool steerCurrent(const QString &text) {
    ++m_steerCount;
    setRunning(true);
    QVariantMap turn;
    turn.insert(QStringLiteral("role"), QStringLiteral("user"));
    turn.insert(QStringLiteral("kind"), QStringLiteral("steer"));
    turn.insert(QStringLiteral("text"), text);
    m_transcript.append(turn);
    emit transcriptChanged();
    return true;
  }
  Q_INVOKABLE void approveTool(const QString &, bool) { ++m_approvalCount; }
  Q_INVOKABLE void denyTool(const QString &) { ++m_approvalCount; }
  Q_INVOKABLE bool loadSkill(const QString &id) {
    ++m_skillLoads;
    if (id.isEmpty()) return false;
    return true;
  }
  Q_INVOKABLE void refreshSkills() {}
  Q_INVOKABLE void acceptAll() { ++m_acceptAllCount; }
  Q_INVOKABLE void rejectAll() { ++m_rejectAllCount; }
  Q_INVOKABLE bool switchSession(const QString &id) {
    ++m_switchCount;
    Q_UNUSED(id)
    return true;
  }
  Q_INVOKABLE bool removeSession(const QString &) { return true; }
  Q_INVOKABLE QString createSession(const QString &title) {
    ++m_createSessionCount;
    m_lastCreatedTitle = title;
    return QStringLiteral("session-new");
  }
  Q_INVOKABLE void renameSession(const QString &, const QString &) {}

signals:
  void runningChanged();
  void agentChanged();
  void lastErrorChanged();
  void connectionErrorChanged();
  void pendingChanged();
  void agentRunsChanged();
  void answerChanged();
  void stepsChanged();
  void transcriptChanged();
  void finishedChanged();
  void promptStarted(const QString &prompt);
  void sessionsChanged();
  void permissionChanged();
  void skillsChanged();
  void compactedChanged();

private:
  bool m_running = false;
  bool m_pending = false;
  QString m_currentAgent;
  QString m_lastError;
  QString m_connectionError;
  QVariantList m_agentRuns;
  QVariantList m_answers;
  QString m_lastAnswer;
  QVariantList m_diff;
  int m_commits = 0;
  int m_rejects = 0;
  int m_prompts = 0;
  int m_regenerates = 0;
  int m_positiveFeedback = 0;
  int m_negativeFeedback = 0;
  QVariantList m_sessions;
  QVariantList m_subagents;
  QVariantList m_permissions;
  QVariantList m_skills;
  bool m_compacted = false;
  int m_liveTokens = 128;
  int m_steerCount = 0;
  int m_approvalCount = 0;
  int m_skillLoads = 0;
  int m_acceptAllCount = 0;
  int m_rejectAllCount = 0;
  int m_switchCount = 0;
  int m_createSessionCount = 0;
  QString m_lastCreatedTitle;
  QVariantList m_steps;
  QVariantList m_transcript;
};
