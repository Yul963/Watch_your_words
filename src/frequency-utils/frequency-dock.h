#pragma once

#include "obs-module.h"
#include "obs-frontend-api.h"
#include "obs-properties.h"
#include "util/c99defs.h"
#include <QDockWidget>
#include <QTextBrowser>
#include <QPushButton>
#include <QVBoxLayout>

// Global pointer for the QDockWidget
extern QDockWidget *myDockWidget;

// Function to handle button click
bool buttonClicked(obs_properties_t *props, obs_property_t *property,
		   void *data);