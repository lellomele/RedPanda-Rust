// Copyright (C) 2026 Raffaele Mele
// Programma autonomo per provare editor, compilatore e console di Red Panda Rust.
// Nessuna dipendenza esterna: aprire questo file .rs e scegliere Esegui Rust.
// Opzioni da terminale: --verbose, --list, --help.

use std::collections::{BTreeMap, BTreeSet, VecDeque};
use std::fmt;
use std::io::{self, Cursor, Read, Seek, SeekFrom, Write};
use std::process::ExitCode;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::thread;

type TestResult = Result<(), TestError>;

#[derive(Debug)]
struct TestError {
    detail: String,
}

impl TestError {
    fn new(detail: impl Into<String>) -> Self {
        Self { detail: detail.into() }
    }
}

impl fmt::Display for TestError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{}", self.detail)
    }
}

impl From<io::Error> for TestError {
    fn from(error: io::Error) -> Self {
        Self::new(format!("Errore I/O: {error}"))
    }
}

#[derive(Default)]
struct TestContext {
    checks: usize,
}

impl TestContext {
    fn expect(&mut self, condition: bool, detail: &str) -> TestResult {
        self.checks += 1;
        if condition {
            Ok(())
        } else {
            Err(TestError::new(detail))
        }
    }
}

struct Scenario {
    name: &'static str,
    execute: fn(&mut TestContext) -> TestResult,
}

#[derive(Clone, Debug, PartialEq)]
struct Matrix<const ROWS: usize, const COLS: usize> {
    data: [[i64; COLS]; ROWS],
}

impl<const ROWS: usize, const COLS: usize> Matrix<ROWS, COLS> {
    fn transpose(&self) -> Matrix<COLS, ROWS> {
        let mut result = [[0; ROWS]; COLS];
        for (row_index, row) in self.data.iter().enumerate() {
            for (column_index, value) in row.iter().enumerate() {
                result[column_index][row_index] = *value;
            }
        }
        Matrix { data: result }
    }

    fn sum(&self) -> i64 {
        self.data.iter().flat_map(|row| row.iter()).copied().sum()
    }
}

#[derive(Debug, PartialEq)]
enum Shape {
    Square(i64),
    Rectangle { width: i64, height: i64 },
    Point,
}

impl Shape {
    fn area(&self) -> i64 {
        match self {
            Self::Square(side) => side * side,
            Self::Rectangle { width, height } => width * height,
            Self::Point => 0,
        }
    }
}

#[derive(Clone, Debug)]
struct Student {
    id: u32,
    name: String,
    scores: Vec<u32>,
}

impl Student {
    fn average(&self) -> Option<f64> {
        if self.scores.is_empty() {
            return None;
        }
        let total: u64 = self.scores.iter().map(|score| u64::from(*score)).sum();
        Some(total as f64 / self.scores.len() as f64)
    }
}

fn find_student<'a>(students: &'a [Student], id: u32) -> Option<&'a Student> {
    students.iter().find(|student| student.id == id)
}

#[derive(Clone, Copy, Debug, PartialEq)]
enum TaskState {
    Pending,
    Running,
    Completed,
    Cancelled,
}

#[derive(Debug)]
struct Task {
    id: u32,
    state: TaskState,
}

impl Task {
    fn start(&mut self) -> TestResult {
        if self.state != TaskState::Pending {
            return Err(TestError::new("Transizione start non valida"));
        }
        self.state = TaskState::Running;
        Ok(())
    }

    fn finish(&mut self) -> TestResult {
        if self.state != TaskState::Running {
            return Err(TestError::new("Transizione finish non valida"));
        }
        self.state = TaskState::Completed;
        Ok(())
    }
}

#[derive(Debug)]
struct TreeNode {
    value: i64,
    children: Vec<TreeNode>,
}

impl TreeNode {
    fn balanced(value: i64, depth: usize) -> Self {
        let children = if depth == 0 {
            Vec::new()
        } else {
            vec![
                Self::balanced(value * 2, depth - 1),
                Self::balanced(value * 2 + 1, depth - 1),
            ]
        };
        Self { value, children }
    }

    fn preorder(&self) -> Vec<i64> {
        let mut values = Vec::new();
        let mut pending = vec![self];
        while let Some(node) = pending.pop() {
            values.push(node.value);
            pending.extend(node.children.iter().rev());
        }
        values
    }

    fn sum_recursive(&self) -> i64 {
        self.value + self.children.iter().map(Self::sum_recursive).sum::<i64>()
    }
}

fn parse_positive(text: &str) -> Result<u64, TestError> {
    let value = text
        .trim()
        .parse::<u64>()
        .map_err(|error| TestError::new(format!("Numero non valido: {error}")))?;
    if value == 0 {
        Err(TestError::new("Il valore deve essere positivo"))
    } else {
        Ok(value)
    }
}

/* Commento di prova per l'editor:
   commenti /* annidati */ e parentesi { } ignorate nella sintassi.
*/

// Scenari: aritmetica

fn scenario_aritmetica_0001(ctx: &mut TestContext) -> TestResult {
    let seed = 1i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0002(ctx: &mut TestContext) -> TestResult {
    let seed = 2i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0003(ctx: &mut TestContext) -> TestResult {
    let seed = 3i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0004(ctx: &mut TestContext) -> TestResult {
    let seed = 4i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0005(ctx: &mut TestContext) -> TestResult {
    let seed = 5i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0006(ctx: &mut TestContext) -> TestResult {
    let seed = 6i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0007(ctx: &mut TestContext) -> TestResult {
    let seed = 7i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0008(ctx: &mut TestContext) -> TestResult {
    let seed = 8i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0009(ctx: &mut TestContext) -> TestResult {
    let seed = 9i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0010(ctx: &mut TestContext) -> TestResult {
    let seed = 10i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0011(ctx: &mut TestContext) -> TestResult {
    let seed = 11i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0012(ctx: &mut TestContext) -> TestResult {
    let seed = 12i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0013(ctx: &mut TestContext) -> TestResult {
    let seed = 13i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0014(ctx: &mut TestContext) -> TestResult {
    let seed = 14i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0015(ctx: &mut TestContext) -> TestResult {
    let seed = 15i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0016(ctx: &mut TestContext) -> TestResult {
    let seed = 16i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

fn scenario_aritmetica_0017(ctx: &mut TestContext) -> TestResult {
    let seed = 17i64;
    let first = seed * 31 + 7;
    let second = seed * 11 + 3;
    let sum = first.checked_add(second).ok_or_else(|| TestError::new("Overflow somma"))?;
    let product = first.checked_mul(second).ok_or_else(|| TestError::new("Overflow prodotto"))?;
    ctx.expect(sum - first == second, "Somma reversibile")?;
    ctx.expect(product / first == second, "Prodotto reversibile")?;
    ctx.expect(product % first == 0, "Divisione esatta")?;
    ctx.expect(i64::MAX.checked_add(1).is_none(), "Overflow intercettato")?;
    ctx.expect(i64::MIN.checked_sub(1).is_none(), "Underflow intercettato")?;
    let bounded = first.clamp(0, 100_000);
    ctx.expect(bounded == first, "Intervallo valido")?;
    let difference = first.abs_diff(second);
    ctx.expect(difference > 0, "Differenza positiva")?;
    Ok(())
}

// Scenari: vettori

fn scenario_vettori_0018(ctx: &mut TestContext) -> TestResult {
    let seed = 18i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0019(ctx: &mut TestContext) -> TestResult {
    let seed = 19i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0020(ctx: &mut TestContext) -> TestResult {
    let seed = 20i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0021(ctx: &mut TestContext) -> TestResult {
    let seed = 21i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0022(ctx: &mut TestContext) -> TestResult {
    let seed = 22i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0023(ctx: &mut TestContext) -> TestResult {
    let seed = 23i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0024(ctx: &mut TestContext) -> TestResult {
    let seed = 24i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0025(ctx: &mut TestContext) -> TestResult {
    let seed = 25i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0026(ctx: &mut TestContext) -> TestResult {
    let seed = 26i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0027(ctx: &mut TestContext) -> TestResult {
    let seed = 27i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0028(ctx: &mut TestContext) -> TestResult {
    let seed = 28i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0029(ctx: &mut TestContext) -> TestResult {
    let seed = 29i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0030(ctx: &mut TestContext) -> TestResult {
    let seed = 30i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0031(ctx: &mut TestContext) -> TestResult {
    let seed = 31i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0032(ctx: &mut TestContext) -> TestResult {
    let seed = 32i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0033(ctx: &mut TestContext) -> TestResult {
    let seed = 33i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

fn scenario_vettori_0034(ctx: &mut TestContext) -> TestResult {
    let seed = 34i64;
    let values: Vec<i64> = (0..32).map(|index| seed + index).collect();
    let doubled: Vec<i64> = values.iter().map(|value| value * 2).collect();
    let total: i64 = values.iter().sum();
    let doubled_total: i64 = doubled.iter().sum();
    ctx.expect(values.len() == 32, "Lunghezza vettore")?;
    ctx.expect(doubled_total == total * 2, "Somma dopo map")?;
    let even: Vec<i64> = values.iter().copied().filter(|value| value % 2 == 0).collect();
    ctx.expect(even.len() == 16, "Filtro pari")?;
    let mut reversed = values.clone();
    reversed.reverse();
    reversed.reverse();
    ctx.expect(reversed == values, "Doppia inversione")?;
    let pairs: Vec<_> = values.iter().zip(doubled.iter()).collect();
    ctx.expect(pairs.iter().all(|(a, b)| **b == **a * 2), "Zip dei vettori")?;
    Ok(())
}

// Scenari: mappe

fn scenario_mappe_0035(ctx: &mut TestContext) -> TestResult {
    let seed = 35u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0036(ctx: &mut TestContext) -> TestResult {
    let seed = 36u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0037(ctx: &mut TestContext) -> TestResult {
    let seed = 37u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0038(ctx: &mut TestContext) -> TestResult {
    let seed = 38u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0039(ctx: &mut TestContext) -> TestResult {
    let seed = 39u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0040(ctx: &mut TestContext) -> TestResult {
    let seed = 40u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0041(ctx: &mut TestContext) -> TestResult {
    let seed = 41u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0042(ctx: &mut TestContext) -> TestResult {
    let seed = 42u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0043(ctx: &mut TestContext) -> TestResult {
    let seed = 43u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0044(ctx: &mut TestContext) -> TestResult {
    let seed = 44u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0045(ctx: &mut TestContext) -> TestResult {
    let seed = 45u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0046(ctx: &mut TestContext) -> TestResult {
    let seed = 46u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0047(ctx: &mut TestContext) -> TestResult {
    let seed = 47u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0048(ctx: &mut TestContext) -> TestResult {
    let seed = 48u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0049(ctx: &mut TestContext) -> TestResult {
    let seed = 49u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0050(ctx: &mut TestContext) -> TestResult {
    let seed = 50u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

fn scenario_mappe_0051(ctx: &mut TestContext) -> TestResult {
    let seed = 51u32;
    let mut inventory = BTreeMap::new();
    for offset in 0..16u32 {
        inventory.insert(format!("articolo_{}", seed + offset), offset + 1);
    }
    ctx.expect(inventory.len() == 16, "Inserimenti distinti")?;
    let quantity: u32 = inventory.values().copied().sum();
    ctx.expect(quantity == 136, "Somma inventario")?;
    let key = format!("articolo_{seed}");
    let original = inventory.get(&key).copied();
    ctx.expect(original == Some(1), "Lettura voce")?;
    if let Some(value) = inventory.get_mut(&key) {
        *value += 10;
    }
    ctx.expect(inventory.get(&key) == Some(&11), "Modifica voce")?;
    ctx.expect(inventory.remove(&key) == Some(11), "Rimozione voce")?;
    ctx.expect(!inventory.contains_key(&key), "Voce rimossa")?;
    Ok(())
}

// Scenari: unicode

fn scenario_unicode_0052(ctx: &mut TestContext) -> TestResult {
    let seed = 52;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0053(ctx: &mut TestContext) -> TestResult {
    let seed = 53;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0054(ctx: &mut TestContext) -> TestResult {
    let seed = 54;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0055(ctx: &mut TestContext) -> TestResult {
    let seed = 55;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0056(ctx: &mut TestContext) -> TestResult {
    let seed = 56;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0057(ctx: &mut TestContext) -> TestResult {
    let seed = 57;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0058(ctx: &mut TestContext) -> TestResult {
    let seed = 58;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0059(ctx: &mut TestContext) -> TestResult {
    let seed = 59;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0060(ctx: &mut TestContext) -> TestResult {
    let seed = 60;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0061(ctx: &mut TestContext) -> TestResult {
    let seed = 61;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0062(ctx: &mut TestContext) -> TestResult {
    let seed = 62;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0063(ctx: &mut TestContext) -> TestResult {
    let seed = 63;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0064(ctx: &mut TestContext) -> TestResult {
    let seed = 64;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0065(ctx: &mut TestContext) -> TestResult {
    let seed = 65;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0066(ctx: &mut TestContext) -> TestResult {
    let seed = 66;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0067(ctx: &mut TestContext) -> TestResult {
    let seed = 67;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

fn scenario_unicode_0068(ctx: &mut TestContext) -> TestResult {
    let seed = 68;
    let greeting = format!("Rust città 🦀 scenario {seed}");
    let bytes = greeting.as_bytes().to_vec();
    let restored = String::from_utf8(bytes).map_err(|error| TestError::new(error.to_string()))?;
    ctx.expect(restored == greeting, "Round trip UTF-8")?;
    ctx.expect(greeting.len() > greeting.chars().count(), "Byte e caratteri distinti")?;
    let words: Vec<&str> = greeting.split_whitespace().collect();
    ctx.expect(words.len() == 5, "Divisione parole Unicode")?;
    let uppercase = greeting.to_uppercase();
    ctx.expect(uppercase.contains("CITTÀ"), "Maiuscole Unicode")?;
    let quoted = r###"{"lingua":"Rust","simbolo":"🦀","testo":"a\\b"}"###;
    ctx.expect(quoted.contains("Rust"), "Stringa raw")?;
    let escaped = "virgolette: \"Rust\"\nseconda riga\ttab";
    ctx.expect(escaped.lines().count() == 2, "Escape newline")?;
    let reconstructed: String = greeting.chars().collect();
    ctx.expect(reconstructed == greeting, "Iteratori di caratteri")?;
    Ok(())
}

// Scenari: enum

fn scenario_enum_0069(ctx: &mut TestContext) -> TestResult {
    let seed = 69i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0070(ctx: &mut TestContext) -> TestResult {
    let seed = 70i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0071(ctx: &mut TestContext) -> TestResult {
    let seed = 71i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0072(ctx: &mut TestContext) -> TestResult {
    let seed = 72i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0073(ctx: &mut TestContext) -> TestResult {
    let seed = 73i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0074(ctx: &mut TestContext) -> TestResult {
    let seed = 74i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0075(ctx: &mut TestContext) -> TestResult {
    let seed = 75i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0076(ctx: &mut TestContext) -> TestResult {
    let seed = 76i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0077(ctx: &mut TestContext) -> TestResult {
    let seed = 77i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0078(ctx: &mut TestContext) -> TestResult {
    let seed = 78i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0079(ctx: &mut TestContext) -> TestResult {
    let seed = 79i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0080(ctx: &mut TestContext) -> TestResult {
    let seed = 80i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0081(ctx: &mut TestContext) -> TestResult {
    let seed = 81i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0082(ctx: &mut TestContext) -> TestResult {
    let seed = 82i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0083(ctx: &mut TestContext) -> TestResult {
    let seed = 83i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0084(ctx: &mut TestContext) -> TestResult {
    let seed = 84i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

fn scenario_enum_0085(ctx: &mut TestContext) -> TestResult {
    let seed = 85i64;
    let square = Shape::Square(seed);
    let rectangle = Shape::Rectangle { width: seed, height: seed + 2 };
    let point = Shape::Point;
    ctx.expect(square.area() == seed * seed, "Area quadrato")?;
    ctx.expect(rectangle.area() == seed * (seed + 2), "Area rettangolo")?;
    ctx.expect(point.area() == 0, "Area punto")?;
    let description = match rectangle {
        Shape::Rectangle { width, height } if width < height => "verticale",
        Shape::Rectangle { .. } => "altro rettangolo",
        Shape::Square(_) | Shape::Point => "altra forma",
    };
    ctx.expect(description == "verticale", "Pattern con guardia")?;
    ctx.expect(matches!(square, Shape::Square(_)), "Macro matches")?;
    Ok(())
}

// Scenari: matrici

fn scenario_matrici_0086(ctx: &mut TestContext) -> TestResult {
    let seed = 86i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0087(ctx: &mut TestContext) -> TestResult {
    let seed = 87i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0088(ctx: &mut TestContext) -> TestResult {
    let seed = 88i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0089(ctx: &mut TestContext) -> TestResult {
    let seed = 89i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0090(ctx: &mut TestContext) -> TestResult {
    let seed = 90i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0091(ctx: &mut TestContext) -> TestResult {
    let seed = 91i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0092(ctx: &mut TestContext) -> TestResult {
    let seed = 92i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0093(ctx: &mut TestContext) -> TestResult {
    let seed = 93i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0094(ctx: &mut TestContext) -> TestResult {
    let seed = 94i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0095(ctx: &mut TestContext) -> TestResult {
    let seed = 95i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0096(ctx: &mut TestContext) -> TestResult {
    let seed = 96i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0097(ctx: &mut TestContext) -> TestResult {
    let seed = 97i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0098(ctx: &mut TestContext) -> TestResult {
    let seed = 98i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0099(ctx: &mut TestContext) -> TestResult {
    let seed = 99i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0100(ctx: &mut TestContext) -> TestResult {
    let seed = 100i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0101(ctx: &mut TestContext) -> TestResult {
    let seed = 101i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

fn scenario_matrici_0102(ctx: &mut TestContext) -> TestResult {
    let seed = 102i64;
    let matrix = Matrix {
        data: [
            [seed, seed + 1, seed + 2],
            [seed + 3, seed + 4, seed + 5],
        ],
    };
    let transposed = matrix.transpose();
    ctx.expect(transposed.data[0] == [seed, seed + 3], "Prima riga trasposta")?;
    ctx.expect(transposed.data[2] == [seed + 2, seed + 5], "Ultima riga trasposta")?;
    ctx.expect(matrix.sum() == seed * 6 + 15, "Somma matrice")?;
    ctx.expect(matrix.sum() == transposed.sum(), "Somma dopo trasposizione")?;
    let round_trip = transposed.transpose();
    ctx.expect(round_trip == matrix, "Doppia trasposizione")?;
    Ok(())
}

// Scenari: record

fn scenario_record_0103(ctx: &mut TestContext) -> TestResult {
    let seed = 103u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0104(ctx: &mut TestContext) -> TestResult {
    let seed = 104u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0105(ctx: &mut TestContext) -> TestResult {
    let seed = 105u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0106(ctx: &mut TestContext) -> TestResult {
    let seed = 106u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0107(ctx: &mut TestContext) -> TestResult {
    let seed = 107u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0108(ctx: &mut TestContext) -> TestResult {
    let seed = 108u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0109(ctx: &mut TestContext) -> TestResult {
    let seed = 109u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0110(ctx: &mut TestContext) -> TestResult {
    let seed = 110u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0111(ctx: &mut TestContext) -> TestResult {
    let seed = 111u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0112(ctx: &mut TestContext) -> TestResult {
    let seed = 112u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0113(ctx: &mut TestContext) -> TestResult {
    let seed = 113u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0114(ctx: &mut TestContext) -> TestResult {
    let seed = 114u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0115(ctx: &mut TestContext) -> TestResult {
    let seed = 115u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0116(ctx: &mut TestContext) -> TestResult {
    let seed = 116u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0117(ctx: &mut TestContext) -> TestResult {
    let seed = 117u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0118(ctx: &mut TestContext) -> TestResult {
    let seed = 118u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

fn scenario_record_0119(ctx: &mut TestContext) -> TestResult {
    let seed = 119u32;
    let students: Vec<Student> = (0..8u32).map(|offset| Student {
        id: seed + offset,
        name: format!("Studente_{}", seed + offset),
        scores: vec![60 + offset, 70 + offset, 80 + offset],
    }).collect();
    let selected = find_student(&students, seed + 3).ok_or_else(|| TestError::new("Studente assente"))?;
    let average = selected.average().ok_or_else(|| TestError::new("Media assente"))?;
    ctx.expect((average - 73.0).abs() < 1e-9, "Media del record")?;
    ctx.expect(!selected.name.is_empty(), "Nome presente")?;
    ctx.expect(find_student(&students, seed + 100).is_none(), "Ricerca assente")?;
    let mut ranking = students.clone();
    ranking.sort_by_key(|student| std::cmp::Reverse(student.scores.iter().sum::<u32>()));
    ctx.expect(ranking.first().map(|student| student.id) == Some(seed + 7), "Ordinamento record")?;
    let empty = Student { id: 0, name: String::from("Senza voti"), scores: Vec::new() };
    ctx.expect(empty.average().is_none(), "Media senza dati")?;
    Ok(())
}

// Scenari: stati

fn scenario_stati_0120(ctx: &mut TestContext) -> TestResult {
    let seed = 120u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0121(ctx: &mut TestContext) -> TestResult {
    let seed = 121u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0122(ctx: &mut TestContext) -> TestResult {
    let seed = 122u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0123(ctx: &mut TestContext) -> TestResult {
    let seed = 123u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0124(ctx: &mut TestContext) -> TestResult {
    let seed = 124u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0125(ctx: &mut TestContext) -> TestResult {
    let seed = 125u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0126(ctx: &mut TestContext) -> TestResult {
    let seed = 126u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0127(ctx: &mut TestContext) -> TestResult {
    let seed = 127u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0128(ctx: &mut TestContext) -> TestResult {
    let seed = 128u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0129(ctx: &mut TestContext) -> TestResult {
    let seed = 129u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0130(ctx: &mut TestContext) -> TestResult {
    let seed = 130u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0131(ctx: &mut TestContext) -> TestResult {
    let seed = 131u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0132(ctx: &mut TestContext) -> TestResult {
    let seed = 132u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0133(ctx: &mut TestContext) -> TestResult {
    let seed = 133u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0134(ctx: &mut TestContext) -> TestResult {
    let seed = 134u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0135(ctx: &mut TestContext) -> TestResult {
    let seed = 135u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

fn scenario_stati_0136(ctx: &mut TestContext) -> TestResult {
    let seed = 136u32;
    let mut pending: VecDeque<Task> = (0..12u32).map(|offset| Task {
        id: seed + offset,
        state: TaskState::Pending,
    }).collect();
    let mut completed = Vec::new();
    while let Some(mut task) = pending.pop_front() {
        task.start()?;
        ctx.expect(task.state == TaskState::Running, "Stato running")?;
        task.finish()?;
        ctx.expect(task.start().is_err(), "Transizione invalida rifiutata")?;
        completed.push(task);
    }
    ctx.expect(completed.len() == 12, "Numero task completati")?;
    ctx.expect(completed.iter().all(|task| task.state == TaskState::Completed), "Stati finali")?;
    ctx.expect(completed.first().map(|task| task.id) == Some(seed), "Ordine FIFO")?;
    let cancelled = Task { id: seed, state: TaskState::Cancelled };
    ctx.expect(cancelled.state != TaskState::Completed, "Annullamento distinto")?;
    Ok(())
}

// Scenari: ordinamento

fn scenario_ordinamento_0137(ctx: &mut TestContext) -> TestResult {
    let seed = 137i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0138(ctx: &mut TestContext) -> TestResult {
    let seed = 138i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0139(ctx: &mut TestContext) -> TestResult {
    let seed = 139i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0140(ctx: &mut TestContext) -> TestResult {
    let seed = 140i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0141(ctx: &mut TestContext) -> TestResult {
    let seed = 141i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0142(ctx: &mut TestContext) -> TestResult {
    let seed = 142i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0143(ctx: &mut TestContext) -> TestResult {
    let seed = 143i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0144(ctx: &mut TestContext) -> TestResult {
    let seed = 144i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0145(ctx: &mut TestContext) -> TestResult {
    let seed = 145i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0146(ctx: &mut TestContext) -> TestResult {
    let seed = 146i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0147(ctx: &mut TestContext) -> TestResult {
    let seed = 147i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0148(ctx: &mut TestContext) -> TestResult {
    let seed = 148i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0149(ctx: &mut TestContext) -> TestResult {
    let seed = 149i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0150(ctx: &mut TestContext) -> TestResult {
    let seed = 150i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0151(ctx: &mut TestContext) -> TestResult {
    let seed = 151i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0152(ctx: &mut TestContext) -> TestResult {
    let seed = 152i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

fn scenario_ordinamento_0153(ctx: &mut TestContext) -> TestResult {
    let seed = 153i64;
    let mut data: Vec<i64> = (0..40).map(|offset| seed + offset % 20).collect();
    data.reverse();
    let original_sum: i64 = data.iter().sum();
    let original_length = data.len();
    data.sort_unstable();
    ctx.expect(data.windows(2).all(|pair| pair[0] <= pair[1]), "Ordine crescente")?;
    ctx.expect(data.iter().sum::<i64>() == original_sum, "Somma conservata")?;
    ctx.expect(data.len() == original_length, "Lunghezza conservata")?;
    ctx.expect(data.binary_search(&(seed + 10)).is_ok(), "Ricerca binaria presente")?;
    ctx.expect(data.binary_search(&(seed + 100)).is_err(), "Ricerca binaria assente")?;
    let distinct: BTreeSet<i64> = data.iter().copied().collect();
    ctx.expect(distinct.len() == 20, "Valori distinti")?;
    data.dedup();
    ctx.expect(data.len() == distinct.len(), "Deduplicazione")?;
    Ok(())
}

// Scenari: thread

fn scenario_thread_0154(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 154usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0155(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 155usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0156(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 156usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0157(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 157usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0158(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 158usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0159(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 159usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0160(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 160usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0161(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 161usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0162(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 162usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0163(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 163usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0164(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 164usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0165(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 165usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0166(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 166usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0167(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 167usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0168(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 168usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0169(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 169usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

fn scenario_thread_0170(ctx: &mut TestContext) -> TestResult {
    let rounds = 16 + 170usize % 16;
    let shared = Arc::new(AtomicUsize::new(0));
    let mut workers = Vec::new();
    for _worker_id in 0..4 {
        let counter = Arc::clone(&shared);
        workers.push(thread::spawn(move || {
            for _round in 0..rounds {
                counter.fetch_add(1, Ordering::Relaxed);
            }
            rounds
        }));
    }
    let mut reported = 0;
    for worker in workers {
        reported += worker.join().map_err(|_| TestError::new("Thread interrotto"))?;
    }
    ctx.expect(reported == rounds * 4, "Lavoro dichiarato")?;
    ctx.expect(shared.load(Ordering::Relaxed) == reported, "Contatore atomico")?;
    Ok(())
}

// Scenari: binario

fn scenario_binario_0171(ctx: &mut TestContext) -> TestResult {
    let seed = 171u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0172(ctx: &mut TestContext) -> TestResult {
    let seed = 172u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0173(ctx: &mut TestContext) -> TestResult {
    let seed = 173u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0174(ctx: &mut TestContext) -> TestResult {
    let seed = 174u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0175(ctx: &mut TestContext) -> TestResult {
    let seed = 175u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0176(ctx: &mut TestContext) -> TestResult {
    let seed = 176u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0177(ctx: &mut TestContext) -> TestResult {
    let seed = 177u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0178(ctx: &mut TestContext) -> TestResult {
    let seed = 178u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0179(ctx: &mut TestContext) -> TestResult {
    let seed = 179u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0180(ctx: &mut TestContext) -> TestResult {
    let seed = 180u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0181(ctx: &mut TestContext) -> TestResult {
    let seed = 181u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0182(ctx: &mut TestContext) -> TestResult {
    let seed = 182u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0183(ctx: &mut TestContext) -> TestResult {
    let seed = 183u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0184(ctx: &mut TestContext) -> TestResult {
    let seed = 184u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0185(ctx: &mut TestContext) -> TestResult {
    let seed = 185u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0186(ctx: &mut TestContext) -> TestResult {
    let seed = 186u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

fn scenario_binario_0187(ctx: &mut TestContext) -> TestResult {
    let seed = 187u64;
    let values = [seed, seed * 1000, seed * seed + 7];
    let mut buffer = Cursor::new(Vec::new());
    for value in values {
        buffer.write_all(&value.to_le_bytes())?;
    }
    ctx.expect(buffer.get_ref().len() == 24, "Dimensione codifica binaria")?;
    buffer.seek(SeekFrom::Start(0))?;
    let mut restored = Vec::new();
    for _index in 0..values.len() {
        let mut bytes = [0u8; 8];
        buffer.read_exact(&mut bytes)?;
        restored.push(u64::from_le_bytes(bytes));
    }
    ctx.expect(restored.as_slice() == values.as_slice(), "Decodifica reversibile")?;
    let mut extra = [0u8; 1];
    ctx.expect(buffer.read_exact(&mut extra).is_err(), "Fine buffer gestita")?;
    Ok(())
}

// Scenari: alberi

fn scenario_alberi_0188(ctx: &mut TestContext) -> TestResult {
    let seed = 188i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0189(ctx: &mut TestContext) -> TestResult {
    let seed = 189i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0190(ctx: &mut TestContext) -> TestResult {
    let seed = 190i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0191(ctx: &mut TestContext) -> TestResult {
    let seed = 191i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0192(ctx: &mut TestContext) -> TestResult {
    let seed = 192i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0193(ctx: &mut TestContext) -> TestResult {
    let seed = 193i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0194(ctx: &mut TestContext) -> TestResult {
    let seed = 194i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0195(ctx: &mut TestContext) -> TestResult {
    let seed = 195i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0196(ctx: &mut TestContext) -> TestResult {
    let seed = 196i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0197(ctx: &mut TestContext) -> TestResult {
    let seed = 197i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0198(ctx: &mut TestContext) -> TestResult {
    let seed = 198i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0199(ctx: &mut TestContext) -> TestResult {
    let seed = 199i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0200(ctx: &mut TestContext) -> TestResult {
    let seed = 200i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0201(ctx: &mut TestContext) -> TestResult {
    let seed = 201i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0202(ctx: &mut TestContext) -> TestResult {
    let seed = 202i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0203(ctx: &mut TestContext) -> TestResult {
    let seed = 203i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenario_alberi_0204(ctx: &mut TestContext) -> TestResult {
    let seed = 204i64;
    let tree = TreeNode::balanced(seed, 3);
    let values = tree.preorder();
    ctx.expect(values.len() == 15, "Numero nodi albero")?;
    ctx.expect(values.first() == Some(&seed), "Radice visitata per prima")?;
    let iterative_sum: i64 = values.iter().sum();
    ctx.expect(iterative_sum == tree.sum_recursive(), "Visite equivalenti")?;
    let leaves: Vec<_> = values.iter().copied().filter(|value| *value >= seed * 8).collect();
    ctx.expect(leaves.len() == 8, "Numero foglie")?;
    let unique: BTreeSet<i64> = values.iter().copied().collect();
    ctx.expect(unique.len() == values.len(), "Valori distinti nell'albero")?;
    let parsed = parse_positive(&format!(" {seed} "))?;
    ctx.expect(parsed == seed as u64, "Parsing con spazi")?;
    ctx.expect(parse_positive("0").is_err(), "Zero rifiutato")?;
    ctx.expect(parse_positive("abc").is_err(), "Input non numerico rifiutato")?;
    Ok(())
}

fn scenarios() -> Vec<Scenario> {
    vec![
        Scenario { name: "scenario_aritmetica_0001", execute: scenario_aritmetica_0001 },
        Scenario { name: "scenario_aritmetica_0002", execute: scenario_aritmetica_0002 },
        Scenario { name: "scenario_aritmetica_0003", execute: scenario_aritmetica_0003 },
        Scenario { name: "scenario_aritmetica_0004", execute: scenario_aritmetica_0004 },
        Scenario { name: "scenario_aritmetica_0005", execute: scenario_aritmetica_0005 },
        Scenario { name: "scenario_aritmetica_0006", execute: scenario_aritmetica_0006 },
        Scenario { name: "scenario_aritmetica_0007", execute: scenario_aritmetica_0007 },
        Scenario { name: "scenario_aritmetica_0008", execute: scenario_aritmetica_0008 },
        Scenario { name: "scenario_aritmetica_0009", execute: scenario_aritmetica_0009 },
        Scenario { name: "scenario_aritmetica_0010", execute: scenario_aritmetica_0010 },
        Scenario { name: "scenario_aritmetica_0011", execute: scenario_aritmetica_0011 },
        Scenario { name: "scenario_aritmetica_0012", execute: scenario_aritmetica_0012 },
        Scenario { name: "scenario_aritmetica_0013", execute: scenario_aritmetica_0013 },
        Scenario { name: "scenario_aritmetica_0014", execute: scenario_aritmetica_0014 },
        Scenario { name: "scenario_aritmetica_0015", execute: scenario_aritmetica_0015 },
        Scenario { name: "scenario_aritmetica_0016", execute: scenario_aritmetica_0016 },
        Scenario { name: "scenario_aritmetica_0017", execute: scenario_aritmetica_0017 },
        Scenario { name: "scenario_vettori_0018", execute: scenario_vettori_0018 },
        Scenario { name: "scenario_vettori_0019", execute: scenario_vettori_0019 },
        Scenario { name: "scenario_vettori_0020", execute: scenario_vettori_0020 },
        Scenario { name: "scenario_vettori_0021", execute: scenario_vettori_0021 },
        Scenario { name: "scenario_vettori_0022", execute: scenario_vettori_0022 },
        Scenario { name: "scenario_vettori_0023", execute: scenario_vettori_0023 },
        Scenario { name: "scenario_vettori_0024", execute: scenario_vettori_0024 },
        Scenario { name: "scenario_vettori_0025", execute: scenario_vettori_0025 },
        Scenario { name: "scenario_vettori_0026", execute: scenario_vettori_0026 },
        Scenario { name: "scenario_vettori_0027", execute: scenario_vettori_0027 },
        Scenario { name: "scenario_vettori_0028", execute: scenario_vettori_0028 },
        Scenario { name: "scenario_vettori_0029", execute: scenario_vettori_0029 },
        Scenario { name: "scenario_vettori_0030", execute: scenario_vettori_0030 },
        Scenario { name: "scenario_vettori_0031", execute: scenario_vettori_0031 },
        Scenario { name: "scenario_vettori_0032", execute: scenario_vettori_0032 },
        Scenario { name: "scenario_vettori_0033", execute: scenario_vettori_0033 },
        Scenario { name: "scenario_vettori_0034", execute: scenario_vettori_0034 },
        Scenario { name: "scenario_mappe_0035", execute: scenario_mappe_0035 },
        Scenario { name: "scenario_mappe_0036", execute: scenario_mappe_0036 },
        Scenario { name: "scenario_mappe_0037", execute: scenario_mappe_0037 },
        Scenario { name: "scenario_mappe_0038", execute: scenario_mappe_0038 },
        Scenario { name: "scenario_mappe_0039", execute: scenario_mappe_0039 },
        Scenario { name: "scenario_mappe_0040", execute: scenario_mappe_0040 },
        Scenario { name: "scenario_mappe_0041", execute: scenario_mappe_0041 },
        Scenario { name: "scenario_mappe_0042", execute: scenario_mappe_0042 },
        Scenario { name: "scenario_mappe_0043", execute: scenario_mappe_0043 },
        Scenario { name: "scenario_mappe_0044", execute: scenario_mappe_0044 },
        Scenario { name: "scenario_mappe_0045", execute: scenario_mappe_0045 },
        Scenario { name: "scenario_mappe_0046", execute: scenario_mappe_0046 },
        Scenario { name: "scenario_mappe_0047", execute: scenario_mappe_0047 },
        Scenario { name: "scenario_mappe_0048", execute: scenario_mappe_0048 },
        Scenario { name: "scenario_mappe_0049", execute: scenario_mappe_0049 },
        Scenario { name: "scenario_mappe_0050", execute: scenario_mappe_0050 },
        Scenario { name: "scenario_mappe_0051", execute: scenario_mappe_0051 },
        Scenario { name: "scenario_unicode_0052", execute: scenario_unicode_0052 },
        Scenario { name: "scenario_unicode_0053", execute: scenario_unicode_0053 },
        Scenario { name: "scenario_unicode_0054", execute: scenario_unicode_0054 },
        Scenario { name: "scenario_unicode_0055", execute: scenario_unicode_0055 },
        Scenario { name: "scenario_unicode_0056", execute: scenario_unicode_0056 },
        Scenario { name: "scenario_unicode_0057", execute: scenario_unicode_0057 },
        Scenario { name: "scenario_unicode_0058", execute: scenario_unicode_0058 },
        Scenario { name: "scenario_unicode_0059", execute: scenario_unicode_0059 },
        Scenario { name: "scenario_unicode_0060", execute: scenario_unicode_0060 },
        Scenario { name: "scenario_unicode_0061", execute: scenario_unicode_0061 },
        Scenario { name: "scenario_unicode_0062", execute: scenario_unicode_0062 },
        Scenario { name: "scenario_unicode_0063", execute: scenario_unicode_0063 },
        Scenario { name: "scenario_unicode_0064", execute: scenario_unicode_0064 },
        Scenario { name: "scenario_unicode_0065", execute: scenario_unicode_0065 },
        Scenario { name: "scenario_unicode_0066", execute: scenario_unicode_0066 },
        Scenario { name: "scenario_unicode_0067", execute: scenario_unicode_0067 },
        Scenario { name: "scenario_unicode_0068", execute: scenario_unicode_0068 },
        Scenario { name: "scenario_enum_0069", execute: scenario_enum_0069 },
        Scenario { name: "scenario_enum_0070", execute: scenario_enum_0070 },
        Scenario { name: "scenario_enum_0071", execute: scenario_enum_0071 },
        Scenario { name: "scenario_enum_0072", execute: scenario_enum_0072 },
        Scenario { name: "scenario_enum_0073", execute: scenario_enum_0073 },
        Scenario { name: "scenario_enum_0074", execute: scenario_enum_0074 },
        Scenario { name: "scenario_enum_0075", execute: scenario_enum_0075 },
        Scenario { name: "scenario_enum_0076", execute: scenario_enum_0076 },
        Scenario { name: "scenario_enum_0077", execute: scenario_enum_0077 },
        Scenario { name: "scenario_enum_0078", execute: scenario_enum_0078 },
        Scenario { name: "scenario_enum_0079", execute: scenario_enum_0079 },
        Scenario { name: "scenario_enum_0080", execute: scenario_enum_0080 },
        Scenario { name: "scenario_enum_0081", execute: scenario_enum_0081 },
        Scenario { name: "scenario_enum_0082", execute: scenario_enum_0082 },
        Scenario { name: "scenario_enum_0083", execute: scenario_enum_0083 },
        Scenario { name: "scenario_enum_0084", execute: scenario_enum_0084 },
        Scenario { name: "scenario_enum_0085", execute: scenario_enum_0085 },
        Scenario { name: "scenario_matrici_0086", execute: scenario_matrici_0086 },
        Scenario { name: "scenario_matrici_0087", execute: scenario_matrici_0087 },
        Scenario { name: "scenario_matrici_0088", execute: scenario_matrici_0088 },
        Scenario { name: "scenario_matrici_0089", execute: scenario_matrici_0089 },
        Scenario { name: "scenario_matrici_0090", execute: scenario_matrici_0090 },
        Scenario { name: "scenario_matrici_0091", execute: scenario_matrici_0091 },
        Scenario { name: "scenario_matrici_0092", execute: scenario_matrici_0092 },
        Scenario { name: "scenario_matrici_0093", execute: scenario_matrici_0093 },
        Scenario { name: "scenario_matrici_0094", execute: scenario_matrici_0094 },
        Scenario { name: "scenario_matrici_0095", execute: scenario_matrici_0095 },
        Scenario { name: "scenario_matrici_0096", execute: scenario_matrici_0096 },
        Scenario { name: "scenario_matrici_0097", execute: scenario_matrici_0097 },
        Scenario { name: "scenario_matrici_0098", execute: scenario_matrici_0098 },
        Scenario { name: "scenario_matrici_0099", execute: scenario_matrici_0099 },
        Scenario { name: "scenario_matrici_0100", execute: scenario_matrici_0100 },
        Scenario { name: "scenario_matrici_0101", execute: scenario_matrici_0101 },
        Scenario { name: "scenario_matrici_0102", execute: scenario_matrici_0102 },
        Scenario { name: "scenario_record_0103", execute: scenario_record_0103 },
        Scenario { name: "scenario_record_0104", execute: scenario_record_0104 },
        Scenario { name: "scenario_record_0105", execute: scenario_record_0105 },
        Scenario { name: "scenario_record_0106", execute: scenario_record_0106 },
        Scenario { name: "scenario_record_0107", execute: scenario_record_0107 },
        Scenario { name: "scenario_record_0108", execute: scenario_record_0108 },
        Scenario { name: "scenario_record_0109", execute: scenario_record_0109 },
        Scenario { name: "scenario_record_0110", execute: scenario_record_0110 },
        Scenario { name: "scenario_record_0111", execute: scenario_record_0111 },
        Scenario { name: "scenario_record_0112", execute: scenario_record_0112 },
        Scenario { name: "scenario_record_0113", execute: scenario_record_0113 },
        Scenario { name: "scenario_record_0114", execute: scenario_record_0114 },
        Scenario { name: "scenario_record_0115", execute: scenario_record_0115 },
        Scenario { name: "scenario_record_0116", execute: scenario_record_0116 },
        Scenario { name: "scenario_record_0117", execute: scenario_record_0117 },
        Scenario { name: "scenario_record_0118", execute: scenario_record_0118 },
        Scenario { name: "scenario_record_0119", execute: scenario_record_0119 },
        Scenario { name: "scenario_stati_0120", execute: scenario_stati_0120 },
        Scenario { name: "scenario_stati_0121", execute: scenario_stati_0121 },
        Scenario { name: "scenario_stati_0122", execute: scenario_stati_0122 },
        Scenario { name: "scenario_stati_0123", execute: scenario_stati_0123 },
        Scenario { name: "scenario_stati_0124", execute: scenario_stati_0124 },
        Scenario { name: "scenario_stati_0125", execute: scenario_stati_0125 },
        Scenario { name: "scenario_stati_0126", execute: scenario_stati_0126 },
        Scenario { name: "scenario_stati_0127", execute: scenario_stati_0127 },
        Scenario { name: "scenario_stati_0128", execute: scenario_stati_0128 },
        Scenario { name: "scenario_stati_0129", execute: scenario_stati_0129 },
        Scenario { name: "scenario_stati_0130", execute: scenario_stati_0130 },
        Scenario { name: "scenario_stati_0131", execute: scenario_stati_0131 },
        Scenario { name: "scenario_stati_0132", execute: scenario_stati_0132 },
        Scenario { name: "scenario_stati_0133", execute: scenario_stati_0133 },
        Scenario { name: "scenario_stati_0134", execute: scenario_stati_0134 },
        Scenario { name: "scenario_stati_0135", execute: scenario_stati_0135 },
        Scenario { name: "scenario_stati_0136", execute: scenario_stati_0136 },
        Scenario { name: "scenario_ordinamento_0137", execute: scenario_ordinamento_0137 },
        Scenario { name: "scenario_ordinamento_0138", execute: scenario_ordinamento_0138 },
        Scenario { name: "scenario_ordinamento_0139", execute: scenario_ordinamento_0139 },
        Scenario { name: "scenario_ordinamento_0140", execute: scenario_ordinamento_0140 },
        Scenario { name: "scenario_ordinamento_0141", execute: scenario_ordinamento_0141 },
        Scenario { name: "scenario_ordinamento_0142", execute: scenario_ordinamento_0142 },
        Scenario { name: "scenario_ordinamento_0143", execute: scenario_ordinamento_0143 },
        Scenario { name: "scenario_ordinamento_0144", execute: scenario_ordinamento_0144 },
        Scenario { name: "scenario_ordinamento_0145", execute: scenario_ordinamento_0145 },
        Scenario { name: "scenario_ordinamento_0146", execute: scenario_ordinamento_0146 },
        Scenario { name: "scenario_ordinamento_0147", execute: scenario_ordinamento_0147 },
        Scenario { name: "scenario_ordinamento_0148", execute: scenario_ordinamento_0148 },
        Scenario { name: "scenario_ordinamento_0149", execute: scenario_ordinamento_0149 },
        Scenario { name: "scenario_ordinamento_0150", execute: scenario_ordinamento_0150 },
        Scenario { name: "scenario_ordinamento_0151", execute: scenario_ordinamento_0151 },
        Scenario { name: "scenario_ordinamento_0152", execute: scenario_ordinamento_0152 },
        Scenario { name: "scenario_ordinamento_0153", execute: scenario_ordinamento_0153 },
        Scenario { name: "scenario_thread_0154", execute: scenario_thread_0154 },
        Scenario { name: "scenario_thread_0155", execute: scenario_thread_0155 },
        Scenario { name: "scenario_thread_0156", execute: scenario_thread_0156 },
        Scenario { name: "scenario_thread_0157", execute: scenario_thread_0157 },
        Scenario { name: "scenario_thread_0158", execute: scenario_thread_0158 },
        Scenario { name: "scenario_thread_0159", execute: scenario_thread_0159 },
        Scenario { name: "scenario_thread_0160", execute: scenario_thread_0160 },
        Scenario { name: "scenario_thread_0161", execute: scenario_thread_0161 },
        Scenario { name: "scenario_thread_0162", execute: scenario_thread_0162 },
        Scenario { name: "scenario_thread_0163", execute: scenario_thread_0163 },
        Scenario { name: "scenario_thread_0164", execute: scenario_thread_0164 },
        Scenario { name: "scenario_thread_0165", execute: scenario_thread_0165 },
        Scenario { name: "scenario_thread_0166", execute: scenario_thread_0166 },
        Scenario { name: "scenario_thread_0167", execute: scenario_thread_0167 },
        Scenario { name: "scenario_thread_0168", execute: scenario_thread_0168 },
        Scenario { name: "scenario_thread_0169", execute: scenario_thread_0169 },
        Scenario { name: "scenario_thread_0170", execute: scenario_thread_0170 },
        Scenario { name: "scenario_binario_0171", execute: scenario_binario_0171 },
        Scenario { name: "scenario_binario_0172", execute: scenario_binario_0172 },
        Scenario { name: "scenario_binario_0173", execute: scenario_binario_0173 },
        Scenario { name: "scenario_binario_0174", execute: scenario_binario_0174 },
        Scenario { name: "scenario_binario_0175", execute: scenario_binario_0175 },
        Scenario { name: "scenario_binario_0176", execute: scenario_binario_0176 },
        Scenario { name: "scenario_binario_0177", execute: scenario_binario_0177 },
        Scenario { name: "scenario_binario_0178", execute: scenario_binario_0178 },
        Scenario { name: "scenario_binario_0179", execute: scenario_binario_0179 },
        Scenario { name: "scenario_binario_0180", execute: scenario_binario_0180 },
        Scenario { name: "scenario_binario_0181", execute: scenario_binario_0181 },
        Scenario { name: "scenario_binario_0182", execute: scenario_binario_0182 },
        Scenario { name: "scenario_binario_0183", execute: scenario_binario_0183 },
        Scenario { name: "scenario_binario_0184", execute: scenario_binario_0184 },
        Scenario { name: "scenario_binario_0185", execute: scenario_binario_0185 },
        Scenario { name: "scenario_binario_0186", execute: scenario_binario_0186 },
        Scenario { name: "scenario_binario_0187", execute: scenario_binario_0187 },
        Scenario { name: "scenario_alberi_0188", execute: scenario_alberi_0188 },
        Scenario { name: "scenario_alberi_0189", execute: scenario_alberi_0189 },
        Scenario { name: "scenario_alberi_0190", execute: scenario_alberi_0190 },
        Scenario { name: "scenario_alberi_0191", execute: scenario_alberi_0191 },
        Scenario { name: "scenario_alberi_0192", execute: scenario_alberi_0192 },
        Scenario { name: "scenario_alberi_0193", execute: scenario_alberi_0193 },
        Scenario { name: "scenario_alberi_0194", execute: scenario_alberi_0194 },
        Scenario { name: "scenario_alberi_0195", execute: scenario_alberi_0195 },
        Scenario { name: "scenario_alberi_0196", execute: scenario_alberi_0196 },
        Scenario { name: "scenario_alberi_0197", execute: scenario_alberi_0197 },
        Scenario { name: "scenario_alberi_0198", execute: scenario_alberi_0198 },
        Scenario { name: "scenario_alberi_0199", execute: scenario_alberi_0199 },
        Scenario { name: "scenario_alberi_0200", execute: scenario_alberi_0200 },
        Scenario { name: "scenario_alberi_0201", execute: scenario_alberi_0201 },
        Scenario { name: "scenario_alberi_0202", execute: scenario_alberi_0202 },
        Scenario { name: "scenario_alberi_0203", execute: scenario_alberi_0203 },
        Scenario { name: "scenario_alberi_0204", execute: scenario_alberi_0204 },
    ]
}

fn run_all(verbose: bool) -> Result<(usize, usize), TestError> {
    let mut context = TestContext::default();
    let all = scenarios();
    let count = all.len();
    for scenario in all {
        let before = context.checks;
        (scenario.execute)(&mut context).map_err(|error| {
            TestError::new(format!("{}: {error}", scenario.name))
        })?;
        if verbose {
            println!("OK {} ({} controlli)", scenario.name, context.checks - before);
        }
    }
    Ok((count, context.checks))
}

fn main() -> ExitCode {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    if arguments.iter().any(|argument| argument == "--help") {
        println!("Test Red Panda Rust: --verbose, --list, --help");
        return ExitCode::SUCCESS;
    }
    if arguments.iter().any(|argument| argument == "--list") {
        for scenario in scenarios() {
            println!("{}", scenario.name);
        }
        return ExitCode::SUCCESS;
    }
    if arguments.iter().any(|argument| argument != "--verbose") {
        eprintln!("Opzione sconosciuta. Usare --help.");
        return ExitCode::from(2);
    }
    let verbose = arguments.iter().any(|argument| argument == "--verbose");
    println!("Red Panda Rust — programma di prova 🦀");
    println!("Test deterministici senza dipendenze esterne e senza file da modificare.");
    match run_all(verbose) {
        Ok((count, checks)) => {
            println!("Completati {count} scenari e {checks} controlli: tutto OK.");
            ExitCode::SUCCESS
        }
        Err(error) => {
            eprintln!("Verifica fallita: {error}");
            ExitCode::FAILURE
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn suite_complete() {
        let result = run_all(false);
        assert!(result.is_ok(), "{result:?}");
        let (count, checks) = result.expect("Suite già verificata");
        assert_eq!(count, 204);
        assert!(checks > 1000);
    }

    #[test]
    fn parsing_failures_are_recoverable() {
        assert!(parse_positive("-1").is_err());
        assert!(parse_positive("").is_err());
        assert_eq!(parse_positive(" 42 ").expect("Numero valido"), 42);
    }

    #[test]
    fn invalid_task_transition_preserves_state() {
        let mut task = Task { id: 1, state: TaskState::Pending };
        assert!(task.finish().is_err());
        assert_eq!(task.state, TaskState::Pending);
        assert!(task.start().is_ok());
        assert!(task.finish().is_ok());
        assert_eq!(task.state, TaskState::Completed);
    }
}
