#!/bin/sh
# Minimal Gradle wrapper launcher. Requires gradle/wrapper/gradle-wrapper.jar
DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
JAVA_EXE=${JAVA_HOME:+$JAVA_HOME/bin/java}
JAVA_EXE=${JAVA_EXE:-java}
exec "$JAVA_EXE" -Xmx64m -Xms64m -Dorg.gradle.appname=gradlew -classpath "$DIR/gradle/wrapper/gradle-wrapper.jar" org.gradle.wrapper.GradleWrapperMain "$@"
