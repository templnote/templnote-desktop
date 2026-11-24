#include "FakeAgent.h"
#include "FakeRegistry.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QUrl>
#include <QVariantMap>

class AgentReviewTests : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    qputenv("TEMPLNOTE_CONFIG_DIR", "/tmp/opencode/tn-agentreview-test");

    m_agent = new FakeAgent(this);
    m_registry = new FakeRegistry(this);
    m_engine = new QQmlApplicationEngine(this);
    connect(m_engine, &QQmlEngine::warnings, this,
            [this](const QList<QQmlError> &warnings) {
              m_warnings.append(warnings);
            });

    m_engine->rootContext()->setContextProperty(QStringLiteral("agentService"),
                                                m_agent);
    m_engine->rootContext()->setContextProperty(QStringLiteral("pluginManager"),
                                                nullptr);
    m_engine->rootContext()->setContextProperty(QStringLiteral("providerManager"),
                                                m_registry);
    m_engine->load(QUrl(QStringLiteral("qrc:/qt/qml/Templnote/AgentReviewTest.qml")));
    QVERIFY(m_engine->rootObjects().size() == 1);
    QVERIFY(qobject_cast<QQuickWindow *>(m_engine->rootObjects().first()));
    m_root = m_engine->rootObjects().first();
    QVERIFY(m_root->findChild<QObject *>(QStringLiteral("reviewCard")));
  }

  void reviewCardBindings() {
    QVariantMap diff;
    diff.insert(QStringLiteral("id"), QStringLiteral("b1"));
    diff.insert(QStringLiteral("kind"), QStringLiteral("changed"));
    diff.insert(QStringLiteral("before"), QStringLiteral("Hello world"));
    diff.insert(QStringLiteral("after"), QStringLiteral("Hello AI"));
    diff.insert(QStringLiteral("type"), QStringLiteral("paragraph"));

    QVariantMap run;
    run.insert(QStringLiteral("id"), QStringLiteral("writer"));
    run.insert(QStringLiteral("label"), QStringLiteral("Writer"));
    run.insert(QStringLiteral("edits"), 1);
    run.insert(QStringLiteral("changed"), true);
    run.insert(QStringLiteral("accepted"), true);
    run.insert(QStringLiteral("running"), false);
    run.insert(QStringLiteral("diff"), QVariantList{diff});

    m_agent->setPendingDiff(QVariantList{diff});
    m_agent->setAgentRuns(QVariantList{run});
    m_agent->setHasPending(true);
    QTest::qWait(100);

    QObject *card = m_root->findChild<QObject *>(QStringLiteral("reviewCard"));
    QVERIFY(card->property("visible").toBool());

    // No JavaScript binding errors in the new card or its delegates.
    for (const QQmlError &warning : m_warnings) {
      const QString text = warning.toString();
      QVERIFY2(!text.contains(QStringLiteral("TypeError")) &&
                   !text.contains(QStringLiteral("ReferenceError")),
               qPrintable(text));
    }
  }

  void panelHasWidthInBareLoader() {
    QObject *loader = m_root->findChild<QObject *>(QStringLiteral("bareLoader"));
    QVERIFY(loader);
    QTest::qWait(100);
    QVERIFY2(loader->property("resolvedWidth").toInt() > 100,
             "prompt panel collapsed to a negative/zero width in a bare Loader");
  }

  void errorBannerShowsAndDismisses() {
    QObject *banner = m_root->findChild<QObject *>(QStringLiteral("promptErrorBanner"));
    QVERIFY(banner);
    QVERIFY(!banner->property("visible").toBool());

    // A provider connection failure reaches the panel even when no pipeline
    // has ever run.
    m_agent->setConnectionError(QStringLiteral("Cannot connect to Google: bad key"));
    QTest::qWait(50);
    QVERIFY(banner->property("visible").toBool());

    // The full provider message is shown, not elided into "...".
    const QString longError = QStringLiteral(
        "HTTP 400: Invalid value at 'reasoning_effort': xhigh is not supported "
        "for this model. Choose another thinking level in the model picker.");
    m_agent->setLastError(longError);
    QTest::qWait(50);
    QVERIFY(banner->property("visible").toBool());
    QObject *errorText =
        m_root->findChild<QObject *>(QStringLiteral("promptErrorText"));
    QVERIFY(errorText);
    QCOMPARE(errorText->property("text").toString(),
             QStringLiteral("Cannot connect to Google: bad key \u2014 ") + longError);
    QVERIFY2(banner->property("height").toInt() > 32,
             "long error should wrap instead of being elided");

    QObject *dismiss = m_root->findChild<QObject *>(QStringLiteral("promptErrorDismiss"));
    QVERIFY(dismiss);
    QVERIFY(QMetaObject::invokeMethod(dismiss, "clicked"));
    QVERIFY(m_agent->connectionError().isEmpty());
    QVERIFY(m_agent->lastError().isEmpty());
    QTest::qWait(50);
    QVERIFY(!banner->property("visible").toBool());
  }

  void answerCardShowsProseAnswer() {
    m_agent->clearErrors();
    QVariantMap answer;
    answer.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    answer.insert(QStringLiteral("agent"), QStringLiteral("Assistant"));
    answer.insert(QStringLiteral("text"), QStringLiteral("Hello from the model."));
    m_agent->setTranscript(QVariantList{answer});
    m_agent->setLastAnswer(QStringLiteral("Hello from the model."));
    QTest::qWait(50);
    QObject *repeater = m_root->findChild<QObject *>(QStringLiteral("transcriptRepeater"));
    QVERIFY(repeater);
    QCOMPARE(repeater->property("count").toInt(), 1);
  }

  void modelPickerShowsModelsAndLevels() {
    QObject *button = m_root->findChild<QObject *>(QStringLiteral("modelPickerButton"));
    QVERIFY(button);
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));

    QObject *picker = m_root->findChild<QObject *>(QStringLiteral("modelPicker"));
    QVERIFY(picker);
    QVERIFY2(picker->property("opened").toBool(), "model button did not open the picker");

    QObject *search = m_root->findChild<QObject *>(QStringLiteral("modelPickerSearch"));
    QVERIFY(search);
    search->setProperty("text", QStringLiteral("flash-lite"));
    QTest::qWait(50);
    search->setProperty("text", QString());
    QTest::qWait(50);

    // Hydrogen supports low/medium/high/xhigh; flash-lite offers minimal too.
    QCOMPARE(m_registry->thinkingLevels(QStringLiteral("google|gemini-4-hydrogen")).size(), 5);
    QCOMPARE(m_registry->thinkingLevels(QStringLiteral("google|gemini-3.5-flash-lite")).size(), 5);

    picker->setProperty("levelModelKey", QStringLiteral("google|gemini-4-hydrogen"));
    QTest::qWait(50);
    QObject *overlay = m_root->findChild<QObject *>(QStringLiteral("modelLevelOverlay"));
    QVERIFY(overlay);
    QVERIFY(overlay->property("visible").toBool());
    QVERIFY(m_registry->setModelThinkingLevel(QStringLiteral("google"),
                                               QStringLiteral("gemini-4-hydrogen"),
                                               QStringLiteral("xhigh")));
    QCOMPARE(m_registry->modelThinkingLevel(QStringLiteral("google"),
                                             QStringLiteral("gemini-4-hydrogen")),
             QStringLiteral("xhigh"));
  }

  void sidebarPanelLoads() {
    m_agent->setTranscript(QVariantList{});
    m_agent->setRunning(false);
    QQmlApplicationEngine engine;
    QList<QQmlError> warnings;
    connect(&engine, &QQmlEngine::warnings, this,
            [&warnings](const QList<QQmlError> &list) {
              warnings.append(list);
            });
    engine.rootContext()->setContextProperty(QStringLiteral("agentService"),
                                             m_agent);
    engine.rootContext()->setContextProperty(QStringLiteral("pluginManager"),
                                             nullptr);
    engine.rootContext()->setContextProperty(QStringLiteral("providerManager"),
                                             m_registry);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Templnote/SidebarPanelTest.qml")));
    QVERIFY(engine.rootObjects().size() == 1);
    QTest::qWait(100);

    QObject *root = engine.rootObjects().first();
    QObject *emptyState =
        root->findChild<QObject *>(QStringLiteral("emptyState"));
    QVERIFY(emptyState);
    QVERIFY(emptyState->property("visible").toBool());

    for (const char *name : {"promptInput", "sendButton", "modelPickerButton",
                             "modelPicker", "reviewCard", "promptErrorBanner"}) {
      QObject *item = root->findChild<QObject *>(QString::fromLatin1(name));
      QVERIFY2(item, qPrintable(QStringLiteral("sidebar panel missing ") +
                                QString::fromLatin1(name)));
    }

    // The composer is still the same control with the same send behaviour.
    QObject *input = root->findChild<QObject *>(QStringLiteral("promptInput"));
    input->setProperty("text", QStringLiteral("hello sidebar"));
    QTest::qWait(20);
    QCOMPARE(input->property("text").toString(),
             QStringLiteral("hello sidebar"));
    const int promptsBefore = m_agent->promptCount();
    QVERIFY(QMetaObject::invokeMethod(
        root->findChild<QObject *>(QStringLiteral("sendButton")), "clicked"));
    QCOMPARE(m_agent->promptCount(), promptsBefore + 1);
    QVERIFY(QMetaObject::invokeMethod(m_agent, "finish"));
    QTest::qWait(50);
    QVERIFY(!emptyState->property("visible").toBool());
    QObject *transcriptRepeater =
        root->findChild<QObject *>(QStringLiteral("transcriptRepeater"));
    QVERIFY(transcriptRepeater);
    QVERIFY(transcriptRepeater->property("count").toInt() > 0);

    // The conversation keeps real padding at both ends instead of letting
    // turns touch the header and the composer.
    QObject *column =
        root->findChild<QObject *>(QStringLiteral("conversationColumn"));
    QVERIFY(column);
    QCOMPARE(column->property("y").toInt(), 16);
    QObject *conversation =
        root->findChild<QObject *>(QStringLiteral("conversation"));
    QVERIFY(conversation);
    QVERIFY(conversation->property("contentHeight").toInt() >
            column->property("implicitHeight").toInt());

    // Live step trace and assistant turns render from the service properties.
    QVariantMap step;
    step.insert(QStringLiteral("label"), QStringLiteral("Searched the document"));
    step.insert(QStringLiteral("kind"), QStringLiteral("tool"));
    step.insert(QStringLiteral("active"), true);
    m_agent->setSteps(QVariantList{step});
    QVariantMap answer;
    answer.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    answer.insert(QStringLiteral("text"), QStringLiteral("Done."));
    answer.insert(QStringLiteral("agent"), QStringLiteral("Assistant"));
    QVariantList turns = m_agent->transcript();
    turns.append(answer);
    m_agent->setTranscript(turns);
    m_agent->setRunning(true);
    QTest::qWait(50);
    QObject *stepRepeater =
        root->findChild<QObject *>(QStringLiteral("stepRepeater"));
    QVERIFY(stepRepeater);
    QVERIFY2(stepRepeater->property("count").toInt() > 0,
             "live step trace did not render pipeline steps");
    QCOMPARE(transcriptRepeater->property("count").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(m_agent, "finish"));

    for (const QQmlError &warning : warnings) {
      const QString text = warning.toString();
      QVERIFY2(!text.contains(QStringLiteral("TypeError")) &&
                   !text.contains(QStringLiteral("ReferenceError")),
               qPrintable(text));
    }
  }

  void panelInteractions() {
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("agentService"),
                                             m_agent);
    engine.rootContext()->setContextProperty(QStringLiteral("pluginManager"),
                                             nullptr);
    engine.rootContext()->setContextProperty(QStringLiteral("providerManager"),
                                             m_registry);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Templnote/SidebarPanelTest.qml")));
    QVERIFY(engine.rootObjects().size() == 1);
    QObject *root = engine.rootObjects().first();
    QTest::qWait(50);
    m_agent->clearErrors();

    // Permission banner: allow / always / deny all reach the service.
    QVariantMap permission;
    permission.insert(QStringLiteral("id"), QStringLiteral("p1"));
    permission.insert(QStringLiteral("action"), QStringLiteral("shell"));
    permission.insert(QStringLiteral("resource"), QStringLiteral("shell"));
    permission.insert(QStringLiteral("state"), QStringLiteral("pending"));
    m_agent->setPermissions(QVariantList{permission});
    QTest::qWait(50);
    QObject *banner = root->findChild<QObject *>(QStringLiteral("permissionBanner"));
    QVERIFY(banner);
    QVERIFY(banner->property("visible").toBool());
    const int approvals = m_agent->approvalCount();
    QVERIFY(QMetaObject::invokeMethod(
        root->findChild<QObject *>(QStringLiteral("allowAlwaysButton")),
        "clicked"));
    QCOMPARE(m_agent->approvalCount(), approvals + 1);

    // Skills popup loads a skill through the service.
    QVariantMap skill;
    skill.insert(QStringLiteral("id"), QStringLiteral("document-editing"));
    skill.insert(QStringLiteral("name"), QStringLiteral("Document editing"));
    skill.insert(QStringLiteral("description"), QStringLiteral("Block tools"));
    m_agent->setSkills(QVariantList{skill});
    QObject *skillsButton = root->findChild<QObject *>(QStringLiteral("skillsButton"));
    QVERIFY(skillsButton);
    QVERIFY(QMetaObject::invokeMethod(skillsButton, "clicked"));
    QTest::qWait(200);
    QObject *picker = root->findChild<QObject *>(QStringLiteral("skillPicker"));
    QVERIFY(picker && picker->property("opened").toBool());
    const int loads = m_agent->skillLoads();
    QObject *panelRoot = root->findChild<QObject *>(QStringLiteral("promptPanelRoot"));
    QVERIFY(panelRoot);
    QVariant loaded;
    QVERIFY(QMetaObject::invokeMethod(panelRoot, "loadFirstSkill",
                                      Q_RETURN_ARG(QVariant, loaded)));
    QVERIFY(loaded.toBool());
    QCOMPARE(m_agent->skillLoads(), loads + 1);

    // Steering: typing while a run is active redirects instead of being lost.
    m_agent->setRunning(true);
    QObject *input = root->findChild<QObject *>(QStringLiteral("promptInput"));
    input->setProperty("text", QStringLiteral("also check the conclusion"));
    const int steers = m_agent->steerCount();
    QVERIFY(QMetaObject::invokeMethod(
        root->findChild<QObject *>(QStringLiteral("sendButton")), "clicked"));
    QCOMPARE(m_agent->steerCount(), steers + 1);
    QCOMPARE(input->property("text").toString(), QString());
    m_agent->setRunning(false);

    // Review shortcuts and compaction chip.
    QVERIFY(QMetaObject::invokeMethod(
        root->findChild<QObject *>(QStringLiteral("acceptAllButton")),
        "clicked"));
    QVERIFY(QMetaObject::invokeMethod(
        root->findChild<QObject *>(QStringLiteral("rejectAllButton")),
        "clicked"));
    QCOMPARE(m_agent->acceptAllCount(), 1);
    QCOMPARE(m_agent->rejectAllCount(), 1);
    m_agent->setCompacted(true);
    QTest::qWait(50);
    QObject *chip = root->findChild<QObject *>(QStringLiteral("compactionChip"));
    QVERIFY(chip && chip->property("visible").toBool());

    // Subagent card click switches to that conversation.
    QVariantMap child;
    child.insert(QStringLiteral("id"), QStringLiteral("child-1"));
    child.insert(QStringLiteral("title"), QStringLiteral("Research"));
    child.insert(QStringLiteral("running"), false);
    child.insert(QStringLiteral("status"), QStringLiteral("idle"));
    m_agent->setSubagents(QVariantList{child});
    QTest::qWait(50);
    QObject *subList = root->findChild<QObject *>(QStringLiteral("subagentList"));
    const int switches = m_agent->switchCount();
    QVariant switched;
    QVERIFY(QMetaObject::invokeMethod(panelRoot, "switchFirstSubagent",
                                      Q_RETURN_ARG(QVariant, switched)));
    QVERIFY(switched.toBool());
    QCOMPARE(m_agent->switchCount(), switches + 1);

    for (const QQmlError &warning : m_warnings) {
      const QString text = warning.toString();
      QVERIFY2(!text.contains(QStringLiteral("TypeError")) &&
                   !text.contains(QStringLiteral("ReferenceError")),
               qPrintable(text));
    }
  }

  void commitAndReject() {
    QObject *accept =
        m_root->findChild<QObject *>(QStringLiteral("acceptButton"));
    QObject *reject =
        m_root->findChild<QObject *>(QStringLiteral("rejectButton"));
    QVERIFY(accept);
    QVERIFY(reject);
    QVERIFY(accept->property("enabled").toBool());

    QVERIFY(QMetaObject::invokeMethod(accept, "clicked"));
    QCOMPARE(m_agent->commitCount(), 1);

    QVERIFY(QMetaObject::invokeMethod(reject, "clicked"));
    QCOMPARE(m_agent->rejectCount(), 1);
  }

private:
  QQmlApplicationEngine *m_engine = nullptr;
  FakeAgent *m_agent = nullptr;
  FakeRegistry *m_registry = nullptr;
  QObject *m_root = nullptr;
  QList<QQmlError> m_warnings;
};

QTEST_MAIN(AgentReviewTests)
#include "tst_agentreview.moc"
