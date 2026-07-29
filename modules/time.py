# -*- coding: utf-8 -*-
"""
Module Temporel natif pour l'interpréteur Nix.
Permet d'ajouter des temporisations, de mesurer des performances ou d'avoir l'heure locale.
"""

import time
from ast_nodes import FuncCallNode
from environment import NixRuntimeError

# ==============================================================================
# ENREGISTREMENT DES FONCTIONS TEMPORELLES
# ==============================================================================

def nix_time_sleep(args, node):
    """
    Met en pause l'exécution du programme Nix pendant X secondes (accepte les décimaux).
    Usage Nix : time_sleep(1.5) # Attend 1 seconde et demie
    """
    if len(args) < 1:
        raise NixRuntimeError("time_sleep requiert 1 argument : (secondes)", node)
    
    try:
        duration = float(args[0])
        if duration < 0:
            raise NixRuntimeError("La durée de pause ne peut pas être négative.", node)
        time.sleep(duration)
        return None
    except ValueError:
        raise NixRuntimeError("La durée passée à time_sleep doit être un nombre valide.", node)


def nix_time_now(args, node):
    """
    Retourne le timestamp actuel (Epoch time en secondes).
    Idéal pour calculer des différences de temps (mesure de performances).
    Usage Nix : debut = time_now()
    """
    return time.time()


def nix_time_format(args, node):
    """
    Formate un timestamp en une chaîne de caractères lisible selon un pattern.
    Usage Nix : date = time_format(time_now(), "%d/%m/%Y %H:%M:%S")
    """
    t = float(args[0]) if len(args) > 0 else time.time()
    fmt = str(args[1]) if len(args) > 1 else "%Y-%m-%d %H:%M:%S"
    
    try:
        local_struct = time.localtime(t)
        return time.strftime(fmt, local_struct)
    except Exception as e:
        raise NixRuntimeError(f"Échec du formatage du temps : {e}", node)


# ==============================================================================
# ROUTEUR ET SETUP DE L'INTERPRÉTEUR
# ==============================================================================

TIME_FUNCTIONS = {
    "time_sleep": nix_time_sleep,
    "time_now": nix_time_now,
    "time_format": nix_time_format,
}

def setup(evaluator):
    """
    Enregistre les fonctionnalités temporelles natives dans l'environnement Nix.
    """
    previous_handler = evaluator.builtin_handlers.get(FuncCallNode)

    def eval_time_func_call(node, env, evaluator):
        if node.name in TIME_FUNCTIONS:
            evaluated_args = [evaluator.evaluate(arg, env) for arg in node.arguments]
            return TIME_FUNCTIONS[node.name](evaluated_args, node)
        
        if previous_handler:
            return previous_handler(node, env, evaluator)
        return evaluator.eval_func_call(node, env)

    evaluator.register_builtin(FuncCallNode, eval_time_func_call)
