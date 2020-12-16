#!/bin/bash
set -eu

GOOGLE_JSON=app/google-services.json
if [ ! -f "$GOOGLE_JSON" ]; then
  echo "No $GOOGLE_JSON, copying dummy"
  cp app/dummy-google-services.json "$GOOGLE_JSON"
fi

# merge settings files
INTEGRATION_PREFS="custom-vio/viotester-integration/android/preferences.xml"
INTEGRATION_ARRAYS="custom-vio/viotester-integration/android/arrays.xml"
ROOT_PREFS="app/src/main/res/xml/root_preferences.xml"
ROOT_ARRAYS="app/src/main/res/values/arrays.xml"

WARN_BANNER="<!--\n\n\n\n AUTO-GENERATED, DO NOT EDIT \n\n\n\n -->"

printf "$WARN_BANNER" > "$ROOT_PREFS"
printf "$WARN_BANNER" > "$ROOT_ARRAYS"
if [ -f "$INTEGRATION_PREFS" ]; then
  echo "has $INTEGRATION_PREFS, merging settings XMLs"
  # Every piece in Android Gradle Plugin and the XML settings mechanism seems to be
  # actively working to prevent doing this any other way
  sed "s_</PreferenceScreen>__g" < preferences.xml >> "$ROOT_PREFS"
  sed "s_<PreferenceScreen>__g" < "$INTEGRATION_PREFS" | sed "s_</PreferenceScreen>__g" >> "$ROOT_PREFS"

  sed "s_</resources>__g" < arrays.xml >> "$ROOT_ARRAYS"
  sed "s_<resources>__g" < "$INTEGRATION_ARRAYS" >> "$ROOT_ARRAYS"
else
  echo "no custom preferences using preferences as-is"
  cat < preferences.xml >> "$ROOT_PREFS"
  cat < arrays.xml >> "$ROOT_ARRAYS"
fi
cat < reset.xml >> "$ROOT_PREFS"
printf "$WARN_BANNER" >> "$ROOT_PREFS"
printf "$WARN_BANNER" >> "$ROOT_ARRAYS"
