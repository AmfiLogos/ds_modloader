// Test module for names_test.py (name check, kit 0.1.32): GONE=0 needs real game names (it must run), GONE=1 needs a
// class this game version does not have (the loader must turn it off at start).
#include "dsl_sdk.h"
DSL_PLUGIN();
DSL_EXPORT int dsl_plugin_init(const DSLHost *h, DSLPlugin *me) {
#if GONE
    void *k = dsl_need_class(h, me, "Refactor.UI", "NoSuchView_NameCheck");
    h->log(me, "NAMEPROBE gone class=%d", k != NULL);
#else
    void *k = dsl_need_class(h, me, "Refactor.UI", "TitleMenuView");
    void *c = dsl_need_code(h, me, "Refactor.UI", "TitleMenuView", "OnBugReportPressed", 0);
    void *mi = dsl_need_method(h, me, "Refactor", "CampaignSaveLoader", "Save", 3);
    int32_t f = dsl_need_field(h, me, "Refactor.Main", "MainFlow", "_currentScene");
    h->log(me, "NAMEPROBE good class=%d code=%d method=%d field=%d", k != NULL, c != NULL, mi != NULL, f);
#endif
    return 0;
}
