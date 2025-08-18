/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigPrinter.hpp                                  :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vvelikov <vvelikov@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/08/09 17:11:40 by vvelikov          #+#    #+#             */
/*   Updated: 2025/08/11 13:30:34 by vvelikov         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONFIG_PRINTER_HPP
#define CONFIG_PRINTER_HPP

#include <iosfwd>
#include "ServerConfig.h"

void printConfigSummary(std::ostream& os, const ServerConfig& cfg);

#endif
